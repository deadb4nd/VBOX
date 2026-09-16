#include "fakeap.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "AP_SPAM";

#define MAX_FRAME_LEN 256

static bool s_ap_initialized = false;
static bool s_ap_running = false;
static uint16_t s_seq_num = 0;
static char s_ap_name[33] = AP_DEFAULT_SSID;

static void ap_apply_config(void);

void ap_set_name(const char *ssid) {
    if (!ssid) {
        return;
    }
    size_t n = strlen(ssid);
    if (n == 0 || n >= sizeof(s_ap_name)) {
        return;
    }
    snprintf(s_ap_name, sizeof(s_ap_name), "%s", ssid);
    if (!s_ap_initialized) {
        ESP_LOGI(TAG, "AP name set to '%s' (applied on start)", s_ap_name);
        return;
    }
    ap_apply_config(); /* live re-broadcast, keeps clients connected */
    ESP_LOGI(TAG, "AP name updated live to '%s'", s_ap_name);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(e->mac),
                 e->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(e->mac),
                 e->aid);
    }
}

void ap_init(void) {
    if (s_ap_initialized) {
        ap_ensure_start();
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    /* STA netif so promiscuous sniffing (recon) can receive frames while
       the softAP keeps running. The STA never associates to anything. */
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ap_ensure_start();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    s_ap_initialized = true;
    ESP_LOGI(TAG, "web AP '%s' ready on 192.168.4.1", s_ap_name);
}

static void ap_apply_config(void) {
    wifi_config_t ap_cfg = {0};
    memcpy(ap_cfg.ap.ssid, s_ap_name, strlen(s_ap_name) + 1);
    ap_cfg.ap.channel = AP_DEFAULT_CHANNEL;
    ap_cfg.ap.max_connection = AP_DEFAULT_MAX_CONNECTIONS;
    ap_cfg.ap.beacon_interval = 100;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
}

void ap_ensure_start(void) {
    ap_apply_config();

    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) {
        s_ap_running = true;
        ESP_LOGI(TAG, "softAP started (ssid=%s)", s_ap_name);
    } else {
        ESP_LOGE(TAG, "softAP start failed: %s", esp_err_to_name(err));
    }
}

void ap_set_channel(uint8_t channel) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    /* cheap debounce so the MAC switches before raw tx */
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void build_beacon(const char *ssid, uint8_t channel, uint8_t *frame,
                         int *len) {
    memset(frame, 0, MAX_FRAME_LEN);

    int ssid_len = strlen(ssid);
    if (ssid_len > 32)
        ssid_len = 32;

    /* ---- 802.11 MAC Header (24 bytes) ---- */
    frame[0] = 0x80;
    frame[1] = 0x00; /* Frame Control: Beacon */
    frame[2] = 0x00;
    frame[3] = 0x00;            /* Duration */
    memset(&frame[4], 0xFF, 6); /* DA: broadcast */

    uint8_t mac[6];
    esp_fill_random(mac, 6);
    mac[0] = (mac[0] & 0xFE) | 0x02;
    memcpy(&frame[10], mac, 6); /* SA  */
    memcpy(&frame[16], mac, 6); /* BSSID */

    /* Sequence control */
    uint16_t seq = (s_seq_num++ & 0x0FFF) << 4;
    frame[22] = seq & 0xFF;
    frame[23] = (seq >> 8) & 0xFF;

    /* ---- Fixed parameters (12 bytes) ---- */
    /* Timestamp [24-31] left zero */
    frame[32] = 0x64; /* Beacon Interval = 100 TU */
    frame[33] = 0x00;
    frame[34] = 0x01; /* Capabilities: ESS (infrastructure) */
    frame[35] = 0x00;

    /* ---- Tagged parameters ---- */
    int pos = 36;

    /* Tag 0: SSID */
    frame[pos++] = 0x00;
    frame[pos++] = (uint8_t)ssid_len;
    memcpy(&frame[pos], ssid, ssid_len);
    pos += ssid_len;

    /* Tag 1: Supported Rates (1, 2, 5.5, 11, 18, 24, 36, 54 Mbps) */
    static const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8b,
                                    0x96, 0x24, 0x30, 0x48, 0x6c};
    memcpy(&frame[pos], rates, sizeof(rates));
    pos += sizeof(rates);

    /* Tag 3: DS Parameter Set (channel) */
    frame[pos++] = 0x03;
    frame[pos++] = 0x01;
    frame[pos++] = channel;

    *len = pos;
}

void ap_run(ap_config_t config, const char *ssid) {
    uint8_t frame[MAX_FRAME_LEN];
    int len = 0;

    build_beacon(ssid, config.WIFI_CHANNEL, frame, &len);

    /* fire 3 raw beacons back-to-back. WiFi is lossy; this makes
       sure a phone scanning nearby doesn't miss the name. */
    for (int i = 0; i < 3; i++) {
        esp_err_t err = esp_wifi_80211_tx(WIFI_IF_AP, frame, len, false);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "tx err %d", err);
        }
    }

    ESP_LOGI(TAG, "Beacon: %s (ch:%d)", ssid, config.WIFI_CHANNEL);
}