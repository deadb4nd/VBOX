#include "actions.h"

#include "ble_spam.h"
#include "fakeap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <stdint.h>
#include <stdio.h>

static const char *TAG = "ACTIONS";

static velo_settings_t *s_settings = NULL;

static TaskHandle_t g_action_task = NULL;
static volatile bool g_kill_action = false;
static action_t g_current_action = ACTION_NONE;

void actions_set_settings(velo_settings_t *s) { s_settings = s; }

const char *actions_name(action_t a) {
    switch (a) {
    case ACTION_WIFI_DEAUTH:
        return "WiFi Deauth";
    case ACTION_BLE_SPAM:
        return "BLE Spam";
    case ACTION_FAKE_AP:
        return "Fake AP";
    default:
        return "None";
    }
}

bool actions_is_running(void) { return g_action_task != NULL; }

action_t actions_current(void) { return g_current_action; }

static void action_task_wrapper(void *pvParameters) {
    action_t action = (action_t)(intptr_t)pvParameters;

    switch (action) {
    case ACTION_WIFI_DEAUTH:
        ESP_LOGI(TAG, "WiFi deauth loop running");
        while (!g_kill_action) {
            // TODO: inject deauth frames here
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        break;

    case ACTION_BLE_SPAM:
        ESP_LOGI(TAG, "BLE kitchen-sink spam running");
        while (!g_kill_action) {
            ble_spam_kitchen_sink_run_once();
            vTaskDelay(pdMS_TO_TICKS(150)); /* rotate payload every 150 ms */
        }
        ble_spam_stop();
        ESP_LOGI(TAG, "Stopping BLE spam sequence...");
        break;

    case ACTION_FAKE_AP: {
        ESP_LOGI(TAG, "Fake AP starting");

        ap_config_t config = {
            .SSID = {0},
            .PASSWORD = {0},
            .WIFI_CHANNEL = AP_DEFAULT_CHANNEL,
            .MAX_CONNECTIONS = AP_DEFAULT_MAX_CONNECTIONS,
        };
        if (s_settings) {
            config.WIFI_CHANNEL = s_settings->fakeap_channel;
            config.MAX_CONNECTIONS = s_settings->fakeap_max_connections;
        }
        create_config(&config);

        ap_set_channel(config.WIFI_CHANNEL);

        uint32_t ssid_count = 0;
        if (s_settings) {
            ssid_count = settings_ssid_count(s_settings);
        }

        while (!g_kill_action) {
            for (uint32_t i = 0; i < ssid_count; i++) {
                if (g_kill_action) {
                    break;
                }
                char ssid[SETTINGS_SSID_MAX_LEN];
                settings_get_ssid(s_settings, i, ssid, sizeof(ssid));
                ap_run(config, ssid);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        ESP_LOGI(TAG, "Stopping Fake AP sequence...");
        esp_wifi_stop();
        ap_ensure_start(); /* bring the web AP back */
        break;
    }

    default:
        break;
    }

    g_action_task = NULL;
    g_current_action = ACTION_NONE;
    vTaskDelete(NULL);
}

bool actions_start(action_t action) {
    if (g_action_task != NULL) {
        ESP_LOGW(TAG, "action already running");
        return false;
    }
    if (action <= ACTION_NONE || action >= ACTION_COUNT) {
        return false;
    }
    if (action == ACTION_BLE_SPAM && s_settings && !s_settings->ble_spam_enabled) {
        ESP_LOGW(TAG, "BLE spam disabled in settings");
        return false;
    }

    g_kill_action = false;
    g_current_action = action;
    BaseType_t ok =
        xTaskCreate(action_task_wrapper, "action", 4096,
                    (void *)(intptr_t)action, 5, &g_action_task);
    if (ok != pdPASS) {
        g_action_task = NULL;
        g_current_action = ACTION_NONE;
        return false;
    }
    ESP_LOGI(TAG, "started: %s", actions_name(action));
    return true;
}

void actions_stop(void) {
    if (g_action_task == NULL) {
        return;
    }
    ESP_LOGI(TAG, "cancelling action");
    g_kill_action = true;
    for (int i = 0; i < 50 && g_action_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (g_action_task != NULL) {
        vTaskDelete(g_action_task);
        g_action_task = NULL;
        g_current_action = ACTION_NONE;
    }
}