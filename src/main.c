#include "actions.h"
#include "ble_spam.h"
#include "fakeap.h"
#include "logic.h"
#include "settings.h"
#include "settings_nvs.h"
#include "web.h"

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <stdint.h>

#define LED_GPIO GPIO_NUM_15    // XIAO ESP32C6 built-in LED (active LOW)
#define BUZZER_GPIO GPIO_NUM_10 // <-- change to your buzzer pin
#define MOTOR_GPIO GPIO_NUM_2
#define BALL_BTN GPIO_NUM_21

#define IDLE_TIMEOUT_US (1500 * 1000)     // 1.5 s
#define WARNING_DURATION_US (2000 * 1000) // 2.0 s

static const char *TAG = "VELO_BOX";

static velo_settings_t g_settings; // global, NVS-backed

static void configure_external_antenna(void) {
  gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_3, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_14, 1);
}

void log_memory_usage() {
  uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  uint32_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  ESP_LOGI("MEM", "Free Heap: %lu bytes | Lowest Historical Free Peak: %lu bytes",
           free_heap, min_free);
}

/* ---------------- LED / buzzer / haptics ---------------- */

static void gpio_out_init(gpio_num_t pin) {
  gpio_config_t conf = {
      .pin_bit_mask = (1ULL << pin),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&conf);
  gpio_set_level(pin, 0);
}

static void led_on(void) { gpio_set_level(LED_GPIO, 0); } // active LOW
static void led_off(void) { gpio_set_level(LED_GPIO, 1); }
static void buzzer_on(void) { gpio_set_level(BUZZER_GPIO, 1); }
static void buzzer_off(void) { gpio_set_level(BUZZER_GPIO, 0); }
static void motor_on(void) { gpio_set_level(MOTOR_GPIO, 1); }
static void motor_off(void) { gpio_set_level(MOTOR_GPIO, 0); }

static void haptic_tick(void) {
  motor_on();
  vTaskDelay(pdMS_TO_TICKS(220));
  motor_off();
  vTaskDelay(pdMS_TO_TICKS(100));
}

static void haptic_warning(void) {
  motor_on();
  vTaskDelay(pdMS_TO_TICKS(350));
  motor_off();
  vTaskDelay(pdMS_TO_TICKS(250));
  motor_on();
  vTaskDelay(pdMS_TO_TICKS(350));
  motor_off();
}

static void haptic_confirm(void) {
  motor_on();
  vTaskDelay(pdMS_TO_TICKS(250));
  motor_off();
}

static void haptic_cancel(void) {
  motor_on();
  vTaskDelay(pdMS_TO_TICKS(220));
  motor_off();
  vTaskDelay(pdMS_TO_TICKS(120));
  motor_on();
  vTaskDelay(pdMS_TO_TICKS(220));
  motor_off();
}

static void haptic_startup(void) {
  motor_on();
  vTaskDelay(pdMS_TO_TICKS(400));
  motor_off();
  vTaskDelay(pdMS_TO_TICKS(250));
  motor_on();
  vTaskDelay(pdMS_TO_TICKS(400));
  motor_off();
}

/* ---------------- state machine ---------------- */

typedef enum {
  STATE_BROWSING,
  STATE_WARNING,
  STATE_RUNNING
} sys_state_t;

static action_t selection_to_action(settings_action_t sel) {
  return (action_t)((int)sel + 1);
}

void app_main(void) {
  log_memory_usage();
  configure_external_antenna(); // NEVER REMOVE
  gpio_out_init(LED_GPIO);
  led_off();
  gpio_out_init(BUZZER_GPIO);
  gpio_out_init(MOTOR_GPIO);

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  settings_init_default(&g_settings);
  settings_nvs_load(&g_settings);
  actions_set_settings(&g_settings);

  ap_init(); // visible softAP "VeloBox" -> 192.168.4.1
  ble_spam_init();
  web_start(&g_settings);

  gpio_config_t ball_conf = {
      .pin_bit_mask = (1ULL << BALL_BTN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&ball_conf);

  haptic_startup();
  ESP_LOGI(TAG, "Ready. Roll ball to browse.");

  sys_state_t g_state = STATE_BROWSING;
  settings_action_t g_selection = g_settings.default_action;
  int g_last_ball = gpio_get_level(BALL_BTN);

  int64_t last_move_time = esp_timer_get_time();
  int64_t warning_start_us = 0;
  sys_state_t g_last_led_state = 0xFF;
  int g_led_tick = 0;

  while (true) {
    int ball_value = gpio_get_level(BALL_BTN);
    int64_t now = esp_timer_get_time();
    bool rolled = has_ball_moved(ball_value, g_last_ball);

    if (rolled) {
      if (g_state == STATE_RUNNING) {
        printf(">>> USER CANCELLED EXECUTION <<<\n");
        actions_stop();
        g_state = STATE_BROWSING;
        haptic_cancel();

      } else if (g_state == STATE_WARNING) {
        printf(">>> USER CANCELLED WARNING <<<\n");
        g_state = STATE_BROWSING;
        haptic_cancel();

      } else { // STATE_BROWSING
        g_selection = (settings_action_t)(((int)g_selection + 1) %
                                          (int)SETTING_ACTION_COUNT);
        settings_set_default_action(&g_settings, g_selection);
        printf("Selected: %s\n",
               actions_name(selection_to_action(g_selection)));
        haptic_tick();
      }
      last_move_time = now;
    }

    /* the web settings page can change the default action while idle */
    if (g_state == STATE_BROWSING && g_selection != g_settings.default_action) {
      g_selection = g_settings.default_action;
    }

    if (g_state == STATE_BROWSING) {
      if ((now - last_move_time) >= IDLE_TIMEOUT_US) {
        g_state = STATE_WARNING;
        warning_start_us = now;
        printf("WARNING: %s will fire in %.1f s\n",
               actions_name(selection_to_action(g_selection)),
               WARNING_DURATION_US / 1000000.0f);
        haptic_warning();
      }

    } else if (g_state == STATE_WARNING) {
      if ((now - warning_start_us) >= WARNING_DURATION_US) {
        action_t a = selection_to_action(g_selection);
        printf("EXECUTE: %s\n", actions_name(a));
        if (actions_start(a)) {
          g_state = STATE_RUNNING;
          haptic_confirm();
        } else {
          printf("EXECUTE declined, back to browsing\n");
          last_move_time = now;
          g_state = STATE_BROWSING;
        }
      }

    } else if (g_state == STATE_RUNNING) {
      /* action finished on its own -> back to browsing */
      if (!actions_is_running()) {
        g_state = STATE_BROWSING;
        last_move_time = now;
        motor_off();
      }
    }

    if (g_state != g_last_led_state) {
      g_last_led_state = g_state;
      g_led_tick = 0;
    }
    g_led_tick++;

    switch (g_state) {
    case STATE_BROWSING:
      led_off();
      buzzer_off();
      break;
    case STATE_WARNING:
      if ((g_led_tick % 20) < 10) {
        led_on();
        buzzer_on();
      } else {
        led_off();
        buzzer_off();
      }
      break;
    case STATE_RUNNING:
      if ((g_led_tick % 4) < 2) {
        led_on();
        buzzer_on();
      } else {
        led_off();
        buzzer_off();
      }
      break;
    }

    g_last_ball = ball_value;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}