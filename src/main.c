#include "ble_spam.h"
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <stdint.h>

#include "ap_utils.h"
#include "fakeap.h"
#include "logic.h"
#include "ssids.h"

static void cancel_current_action(void);

void log_memory_usage() {
    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI("MEM",
             "Free Heap: %lu bytes | Lowest Historical Free Peak: %lu bytes",
             free_heap, min_free);
}

#define MOTOR_GPIO GPIO_NUM_2
#define BALL_BTN GPIO_NUM_21

#define IDLE_TIMEOUT_US (1500 * 1000)     // 1.5 s
#define WARNING_DURATION_US (2000 * 1000) // 2.0 s

static const char *TAG = "VELO_BOX";

static void configure_external_antenna(void) {
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_3, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 1);
}

static void motor_on(void) { gpio_set_level(MOTOR_GPIO, 1); }
static void motor_off(void) { gpio_set_level(MOTOR_GPIO, 0); }

static void haptic_tick(void) {
    motor_on();
    vTaskDelay(pdMS_TO_TICKS(220));
    motor_off();
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void haptic_warning(void) {
    printf("HAPTIC: Warning\n");
    motor_on();
    vTaskDelay(pdMS_TO_TICKS(350));
    motor_off();
    vTaskDelay(pdMS_TO_TICKS(250));
    motor_on();
    vTaskDelay(pdMS_TO_TICKS(350));
    motor_off();
}

static void haptic_confirm(void) {
    printf("HAPTIC: Fire confirm\n");
    motor_on();
    vTaskDelay(pdMS_TO_TICKS(250));
    motor_off();
}

static void haptic_cancel(void) {
    printf("HAPTIC: Cancelled\n");
    motor_on();
    vTaskDelay(pdMS_TO_TICKS(220));
    motor_off();
    vTaskDelay(pdMS_TO_TICKS(120));
    motor_on();
    vTaskDelay(pdMS_TO_TICKS(220));
    motor_off();
}

static void haptic_startup(void) {
    printf("HAPTIC: Startup\n");
    motor_on();
    vTaskDelay(pdMS_TO_TICKS(400));
    motor_off();
    vTaskDelay(pdMS_TO_TICKS(250));
    motor_on();
    vTaskDelay(pdMS_TO_TICKS(400));
    motor_off();
}

typedef enum {
    ACTION_WIFI_DEAUTH = 0,
    ACTION_BLE_SPAM,
    ACTION_FAKE_AP,
    ACTION_COUNT
} action_t;

static const char *action_names[] = {"WiFi Deauth", "BLE Spam", "Fake AP"};

typedef enum { STATE_BROWSING, STATE_WARNING, STATE_RUNNING } sys_state_t;

static sys_state_t g_state = STATE_BROWSING;
static action_t g_selection = ACTION_WIFI_DEAUTH;
static int g_last_ball = 0;

static TaskHandle_t g_action_task = NULL;
static volatile bool g_kill_action = false;

static void action_task_wrapper(void *pvParameters) {
    action_t action = (action_t)(intptr_t)pvParameters;

    switch (action) {
    case ACTION_WIFI_DEAUTH:
        printf("TASK: WiFi deauth loop running\n");
        while (!g_kill_action) {
            // TODO: inject deauth frames here
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        break;

    case ACTION_BLE_SPAM:
        printf("TASK: BLE spam loop running\n");
        while (!g_kill_action) {
            ble_spam_run_once();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ble_spam_stop();
        printf("TASK: Stopping BLE spam sequence...\n");
        break;

    case ACTION_FAKE_AP: {
        printf("TASK: Fake AP starting\n");
        ap_config_t config = {
            .SSID = "HELLO",
            .MAX_CONNECTIONS = 4,
            .WIFI_CHANNEL = 2,
        };
        create_config(&config);

        int ssids_len = count_ssids(CUSTOM_SSIDS);
        while (!g_kill_action) {
            for (int i = 0; i < ssids_len; i++) {
                if (g_kill_action)
                    break;
                ap_run(config, CUSTOM_SSIDS[i]);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        printf("TASK: Stopping Fake AP sequence...\n");
        esp_wifi_stop();
        break;
    }

    default:
        break;
    }

    g_action_task = NULL;
    vTaskDelete(NULL);
}

static void start_action(action_t action) {
    if (g_action_task != NULL) {
        cancel_current_action();
    }
    g_kill_action = false;
    xTaskCreate(action_task_wrapper, "action", 4096, (void *)(intptr_t)action,
                5, &g_action_task);
}

static void cancel_current_action(void) {
    printf("Cancelling action...\n");
    if (g_action_task != NULL) {
        g_kill_action = true;
        for (int i = 0; i < 50 && g_action_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (g_action_task != NULL) {
            vTaskDelete(g_action_task);
            g_action_task = NULL;
        }
    }
    if (g_selection == ACTION_FAKE_AP) {
        esp_wifi_stop();
    }
    motor_off();
}

void app_main(void) {
    log_memory_usage();
    configure_external_antenna(); // NEVER REMOVE

    /* One-time NVS init (needed by WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_ap();
    ble_spam_init();

    gpio_config_t motor_conf = {
        .pin_bit_mask = (1ULL << MOTOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&motor_conf);
    motor_off();

    gpio_config_t ball_conf = {
        .pin_bit_mask = (1ULL << BALL_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ball_conf);

    int64_t last_move_time = esp_timer_get_time();
    int64_t warning_start_us = 0;

    haptic_startup();
    ESP_LOGI(TAG, "Ready. Roll ball to browse.");

    while (true) {
        int ball_value = gpio_get_level(BALL_BTN);
        int64_t now = esp_timer_get_time();
        bool rolled = has_ball_moved(ball_value, g_last_ball);

        if (rolled) {
            if (g_state == STATE_RUNNING) {
                printf(">>> USER CANCELLED EXECUTION <<<\n");
                cancel_current_action();
                g_state = STATE_BROWSING;
                haptic_cancel();

            } else if (g_state == STATE_WARNING) {
                printf(">>> USER CANCELLED WARNING <<<\n");
                g_state = STATE_BROWSING;
                haptic_cancel();

            } else { // STATE_BROWSING
                update((int *)&g_selection);
                printf("Selected: %s\n", action_names[g_selection]);
                haptic_tick();
            }
            last_move_time = now;
        }

        if (g_state == STATE_BROWSING) {
            if ((now - last_move_time) >= IDLE_TIMEOUT_US) {
                g_state = STATE_WARNING;
                warning_start_us = now;
                printf("WARNING: %s will fire in %.1f s\n",
                       action_names[g_selection],
                       WARNING_DURATION_US / 1000000.0f);
                haptic_warning();
            }

        } else if (g_state == STATE_WARNING) {
            if ((now - warning_start_us) >= WARNING_DURATION_US) {
                printf("EXECUTE: %s\n", action_names[g_selection]);
                g_state = STATE_RUNNING;
                haptic_confirm();
                start_action(g_selection);
            }

        } else if (g_state == STATE_RUNNING) {
            /* If background task dies on its own, reset idle timer so
               we don't immediately warn again. */
            if (g_action_task == NULL) {
                g_state = STATE_BROWSING;
                last_move_time = now;
                motor_off();
            }
        }

        g_last_ball = ball_value;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
