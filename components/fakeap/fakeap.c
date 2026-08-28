#include "fakeap.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <ap_config.h>
#include <ap_utils.h>
#include <ssids.h>
#include <string.h>

static const char *TAG = "AP SPAMMA";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event =
            (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac),
                 event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event =
            (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

void init_ap() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
}

void ap_run(ap_config_t config, const char *ssid) {
    memset(config.SSID, 0, sizeof(config.SSID));
    strncpy((char *)config.SSID, ssid, sizeof(config.SSID) - 1);

    wifi_config_t wifi_config = {
        .ap =
            {
                .ssid_len = strlen((const char *)config.SSID),
                .channel = config.WIFI_CHANNEL,
                .max_connection = config.MAX_CONNECTIONS,
                .authmode = WIFI_AUTH_WPA2_PSK, // or your SAE logic
                .pmf_cfg = {.required = true},
            },
    };
    memcpy(wifi_config.ap.ssid, config.SSID, sizeof(config.SSID));
    memcpy(wifi_config.ap.password, config.PASSWORD, sizeof(config.PASSWORD));

    if (strlen((const char *)config.PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // stop if running, reconfigure, start
    esp_err_t error = esp_wifi_stop();

    if (error != ESP_OK) {
        ESP_LOGI(TAG, "Error while stopping WiFi: %d", error);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Broadcasting: %s", config.SSID);
}

void create_fake_ap(ap_config_t config) {
    create_config(&config);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_ap();

    int ssids_len = count_ssids(CUSTOM_SSIDS);
    while (true) {
        for (int i = 0; i < ssids_len; i++) {
            ap_run(config, CUSTOM_SSIDS[i]);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
