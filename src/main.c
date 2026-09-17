#include "actions.h"
#include "ble_spam.h"
#include "fakeap.h"
#include "logic.h"
#include "power.h"
#include "recon.h"
#include "settings.h"
#include "settings_nvs.h"
#include "web.h"

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <stdint.h>

#define LED_GPIO GPIO_NUM_15    // XIAO ESP32C6 built-in LED (active LOW)
#define BUZZER_GPIO GPIO_NUM_10 // <-- change to your buzzer pin
#define MOTOR_GPIO GPIO_NUM_2
#define BALL_BTN GPIO_NUM_21

#define LOOP_DELAY_MS 50

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

/*
 * Non-blocking haptic scheduler. Patterns are queued as (on, off) pulse
 * trains and advanced from the main loop via haptic_update(), so the loop
 * never stalls and rolls are never missed while the motor is running.
 */
typedef enum { HPHASE_STOPPED, HPHASE_ON, HPHASE_GAP } haptic_phase_t;

static struct {
  bool active;
  bool repeat; /* heartbeat mode until next trigger/haptic_stop_all() */
  haptic_phase_t phase;
  int32_t phase_ms;
  uint8_t pulses_left;
  uint16_t on_ms;
  uint16_t off_ms;
} g_haptic = {.phase = HPHASE_STOPPED};

static void haptic_trigger(uint8_t pulses, uint16_t on_ms, uint16_t off_ms) {
  g_haptic.active = true;
  g_haptic.repeat = false;
  g_haptic.pulses_left = pulses;
  g_haptic.on_ms = on_ms;
  g_haptic.off_ms = off_ms;
  g_haptic.phase = HPHASE_ON;
  g_haptic.phase_ms = (int32_t)on_ms;
  motor_on();
}

static void haptic_repeat(uint16_t on_ms, uint16_t off_ms) {
  g_haptic.active = true;
  g_haptic.repeat = true;
  g_haptic.pulses_left = 0;
  g_haptic.on_ms = on_ms;
  g_haptic.off_ms = off_ms;
  g_haptic.phase = HPHASE_ON;
  g_haptic.phase_ms = (int32_t)on_ms;
  motor_on();
}

static void haptic_stop_all(void) {
  g_haptic.active = false;
  g_haptic.repeat = false;
  g_haptic.phase = HPHASE_STOPPED;
  motor_off();
}

static void haptic_update(int32_t dt_ms) {
  if (!g_haptic.active || dt_ms <= 0) {
    return;
  }
  g_haptic.phase_ms -= dt_ms;
  if (g_haptic.phase_ms > 0) {
    return;
  }

  if (g_haptic.phase == HPHASE_ON) {
    motor_off();
    if (g_haptic.repeat) {
      g_haptic.phase = HPHASE_GAP;
      g_haptic.phase_ms = (int32_t)g_haptic.off_ms;
    } else if (g_haptic.pulses_left > 1) {
      g_haptic.pulses_left--;
      g_haptic.phase = HPHASE_GAP;
      g_haptic.phase_ms = (int32_t)g_haptic.off_ms;
    } else {
      g_haptic.active = false;
      g_haptic.phase = HPHASE_STOPPED;
    }
  } else { /* HPHASE_GAP */
    g_haptic.phase = HPHASE_ON;
    g_haptic.phase_ms = (int32_t)g_haptic.on_ms;
    motor_on();
  }
}

/* Pocket-feel patterns: n pulses = n = (selection + 1), so 1 = Deauth,
   2 = BLE, 3 = FakeAP. The LED mirrors it with n blinks. */
static int g_sel_flash = 0;      // LED blinks still to display
static int g_sel_flash_ticks = 0; // within-blink tick counter
static settings_action_t g_selection = SETTING_ACTION_DEAUTH;

static void selection_feedback(settings_action_t sel) {
  if (sel == SETTING_ACTION_OFF) {
    haptic_trigger(1, 320, 0); /* one long pulse = safe mode */
    g_sel_flash = 2;
    g_sel_flash_ticks = 0;
    return;
  }
  int n = (int)sel + 1;
  haptic_trigger((uint8_t)n, 90, 110);
  g_sel_flash = n;
  g_sel_flash_ticks = 0;
}

static void haptic_startup(void) { haptic_trigger(2, 250, 240); }
static void haptic_confirm(void) { haptic_trigger(1, 180, 0); }
static void haptic_cancel(void) { haptic_trigger(2, 120, 110); }

/* ---------------- state machine ---------------- */

typedef enum {
  STATE_BROWSING,
  STATE_WARNING,
  STATE_RUNNING
} sys_state_t;

static action_t selection_to_action(settings_action_t sel) {
  if (sel == SETTING_ACTION_OFF) {
    return ACTION_NONE;
  }
  return (action_t)((int)sel + 1);
}

static void enter_safe_mode(void) {
  g_selection = SETTING_ACTION_OFF;
  settings_set_default_action(&g_settings, SETTING_ACTION_OFF);
  g_sel_flash = 0;
  ESP_LOGI(TAG, ">> SAFE MODE - will not fire (roll to arm)");
}

/* Low-power idle. NOTE: on the ESP32-C6 only GPIO0..7 can wake from *deep*
   sleep (SOC_GPIO_HP_PERIPH_PD_SLEEP_WAKEABLE_MASK), and the ball switch is
   on GPIO21, so deep sleep can never see a tilt. Light sleep instead: its
   level wake works on any digital GPIO and wakes the instant the ball rolls.
   We reboot on wake so WiFi/AP come back cleanly and the box always returns
   in safe mode; a long timer keepalive means it can never wedge asleep. */
#define SLEEP_KEEPALIVE_US (30LL * 60 * 1000000) /* 30 min */

static void enter_sleep(void) {
  int64_t idle = power_idle_ms(esp_timer_get_time());
  ESP_LOGI(TAG, ">> sleep after %lld ms idle - roll the ball to wake",
           (long long)idle);
  haptic_stop_all();
  led_off();
  buzzer_off();
  motor_off();

  /* park the radios so we genuinely save power and don't fight the sleep */
  ble_spam_stop();
  ble_scan_stop();
  esp_wifi_stop();

  /* rest = whatever level the ball sits at; wake on the opposite edge, so a
     roll in either mounting orientation wakes us and we never self-trigger */
  int rest = gpio_get_level(BALL_BTN);
  gpio_int_type_t wake_level = rest ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
  gpio_set_direction(BALL_BTN, GPIO_MODE_INPUT);
  gpio_pullup_en(BALL_BTN);
  gpio_pulldown_dis(BALL_BTN);
  gpio_wakeup_enable(BALL_BTN, wake_level);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(SLEEP_KEEPALIVE_US);

  esp_light_sleep_start();
  esp_restart(); /* ball tilt and 30-min keepalive both land here */
}

/* track the active label so "done" logging survives action reset */
static const char *g_run_label = "None";

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

  ap_set_name(g_settings.ap_ssid); // before the AP comes up
  ap_init(); // visible softAP (name from settings) -> 192.168.4.1
  ble_spam_init();
  recon_init();
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

  ESP_LOGI(TAG, "----- boot -----");
  ESP_LOGI(TAG, "default action : %s", actions_name(selection_to_action(g_settings.default_action)));
  ESP_LOGI(TAG, "idle timeout   : %u ms", g_settings.idle_timeout_ms);
  ESP_LOGI(TAG, "warning window : %u ms", g_settings.warning_duration_ms);
  ESP_LOGI(TAG, "fakeap         : ch %u, %u conn, %u TU", g_settings.fakeap_channel,
           g_settings.fakeap_max_connections, g_settings.fakeap_beacon_interval);
  ESP_LOGI(TAG, "ble spam       : %s", g_settings.ble_spam_enabled ? "enabled" : "disabled");
  ESP_LOGI(TAG, "sleep idle     : %lu ms%s", (unsigned long)g_settings.sleep_timeout_ms,
           g_settings.sleep_timeout_ms ? "" : " (off)");
  ESP_LOGI(TAG, "ssids          : %u loaded", g_settings.ssid_count);
  ESP_LOGI(TAG, "web            : http://192.168.4.1/");
  ESP_LOGI(TAG, "ready - roll the ball to arm (1 buzz = deauth, 2 = BLE, 3 = fakeap)");
  ESP_LOGI(TAG, "----- boot done -----");

  sys_state_t g_state = STATE_BROWSING;
  g_selection = g_settings.default_action;
  int g_last_ball = gpio_get_level(BALL_BTN);

  int64_t last_move_time = esp_timer_get_time();
  power_note_activity(esp_timer_get_time());
  int64_t last_tick_us = esp_timer_get_time();
  int64_t warning_start_us = 0;
  sys_state_t g_last_led_state = 0xFF;
  int g_led_tick = 0;
  bool g_prev_running = false;

  while (true) {
    int ball_value = gpio_get_level(BALL_BTN);
    int64_t now = esp_timer_get_time();
    int32_t dt_ms = (int32_t)((now - last_tick_us) / 1000);
    last_tick_us = now;
    bool rolled = has_ball_moved(ball_value, g_last_ball);

    if (rolled) {
      if (g_state == STATE_RUNNING) {
        ESP_LOGI(TAG, ">> stopping: %s", g_run_label);
        haptic_stop_all();
        actions_stop();
        g_state = STATE_BROWSING;
        enter_safe_mode();
        haptic_cancel();

      } else if (g_state == STATE_WARNING) {
        ESP_LOGI(TAG, ">> warning cancelled");
        g_state = STATE_BROWSING;
        enter_safe_mode();
        haptic_stop_all();
        haptic_cancel();

      } else { // STATE_BROWSING
        g_selection = (settings_action_t)(((int)g_selection + 1) %
                                          (int)SETTING_ACTION_COUNT);
        settings_set_default_action(&g_settings, g_selection);
        if (g_selection == SETTING_ACTION_OFF) {
          ESP_LOGI(TAG, ">> SAFE MODE - will not fire (roll to arm)");
        } else {
          char summary[96];
          settings_action_summary(&g_settings, g_selection, summary,
                                  sizeof(summary));
          ESP_LOGI(TAG, ">> armed: %s (%d buzz%s) [%s]",
                   actions_name(selection_to_action(g_selection)),
                   (int)g_selection + 1,
                   (int)g_selection == 0 ? "" : "es", summary);
        }
        selection_feedback(g_selection);
      }
      last_move_time = now;
      power_note_activity(now);
    }

    /* the web settings page can change the default action while idle */
    if (g_state == STATE_BROWSING && g_selection != g_settings.default_action) {
      g_selection = g_settings.default_action;
      if (g_selection == SETTING_ACTION_OFF) {
        ESP_LOGI(TAG, ">> SAFE MODE via web - will not fire");
        haptic_trigger(1, 320, 0);
        g_sel_flash = 2;
        g_sel_flash_ticks = 0;
      } else {
        ESP_LOGI(TAG, ">> default action updated via web: %s",
                 actions_name(selection_to_action(g_selection)));
      }
    }

    if (g_state == STATE_BROWSING) {
      if (g_selection == SETTING_ACTION_OFF) {
        /* safe mode: never auto-arm, pocket rolls do nothing */
      } else if ((now - last_move_time) >=
                 (int64_t)g_settings.idle_timeout_ms * 1000) {
        g_state = STATE_WARNING;
        warning_start_us = now;
        char summary[96];
        settings_action_summary(&g_settings, g_selection, summary,
                                sizeof(summary));
        ESP_LOGI(TAG, ">> WARNING: %s fires in %u ms - roll to cancel [%s]",
                 actions_name(selection_to_action(g_selection)),
                 g_settings.warning_duration_ms, summary);
        haptic_repeat(70, 260); // heartbeat so it is felt from a pocket
      }

    } else if (g_state == STATE_WARNING) {
      if ((now - warning_start_us) >= (int64_t)g_settings.warning_duration_ms * 1000) {
        action_t a = selection_to_action(g_selection);
        if (actions_start(a)) {
          g_state = STATE_RUNNING;
          g_run_label = actions_name(a);
          ESP_LOGI(TAG, ">> FIRING: %s", g_run_label);
          haptic_confirm();
        } else {
          ESP_LOGI(TAG, ">> %s declined - back to browsing", actions_name(a));
          last_move_time = now;
          g_state = STATE_BROWSING;
          g_sel_flash = 0;
        }
      }

    } else if (g_state == STATE_RUNNING) {
      /* action finished on its own -> safe mode (never auto re-fire) */
      if (!actions_is_running()) {
        ESP_LOGI(TAG, ">> done: %s - now safe (no loop)", g_run_label);
        g_state = STATE_BROWSING;
        last_move_time = now;
        haptic_stop_all();
        enter_safe_mode();
      }
    }

    /* action started outside the local arm flow (web page) -> pull the
       state machine into RUNNING so LED/buzzer/confirm feedback happens */
    bool running_now = actions_is_running();
    if (running_now && !g_prev_running && g_state != STATE_RUNNING) {
      haptic_stop_all();
      g_state = STATE_RUNNING;
      g_run_label = actions_name(actions_current());
      ESP_LOGI(TAG, ">> FIRING via web: %s", g_run_label);
      haptic_confirm();
      last_move_time = now;
    }
    g_prev_running = running_now;
    if (running_now) {
      power_note_activity(now);
    }

    if (g_state != g_last_led_state) {
      g_last_led_state = g_state;
      g_led_tick = 0;
    }
    g_led_tick++;

    switch (g_state) {
    case STATE_BROWSING:
      buzzer_off();
      if (g_sel_flash > 0) {
        g_sel_flash_ticks++;
        if (g_sel_flash_ticks >= 10) {
          g_sel_flash_ticks = 0;
          g_sel_flash--;
        }
        if (g_sel_flash_ticks < 5) {
          led_on();
        } else {
          led_off();
        }
      } else if (g_selection == SETTING_ACTION_OFF) {
        /* safe mode: single blip every ~5 s so you know it is alive */
        if ((g_led_tick % 100) < 2) {
          led_on();
        } else {
          led_off();
        }
      } else {
        led_off();
      }
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

    haptic_update(dt_ms);

    if (!running_now && g_state == STATE_BROWSING &&
        power_should_sleep(now, g_settings.sleep_timeout_ms)) {
      enter_sleep();
    }

    g_last_ball = ball_value;
    vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
  }
}