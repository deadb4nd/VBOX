#include "recon.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "RECON";

static SemaphoreHandle_t s_mux = NULL;
static volatile bool s_enabled = false;

/* ------------------------------------------------------------------ */
/*  Promiscuous callback                                              */
/* ------------------------------------------------------------------ */

static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    (void)type;
    if (!s_enabled || buf == NULL) {
        return;
    }
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint32_t len = pkt->rx_ctrl.sig_len;
    if (len > 300) {
        len = 300;
    }
    if (len == 0) {
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* Drop the frame if the table mutex is contended; packet loss here is
       fine for a scan tool and keeps httpd callbacks responsive. */
    if (xSemaphoreTake(s_mux, 0) == pdTRUE) {
        recon_core_parse(pkt->payload, len, pkt->rx_ctrl.channel,
                         (int8_t)pkt->rx_ctrl.rssi, now_ms);
        xSemaphoreGive(s_mux);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                       */
/* ------------------------------------------------------------------ */

void recon_init(void) {
    if (s_mux == NULL) {
        s_mux = xSemaphoreCreateMutex();
    }
}

void recon_reset(void) {
    xSemaphoreTake(s_mux, portMAX_DELAY);
    recon_core_reset();
    xSemaphoreGive(s_mux);
}

bool recon_start(void) {
    recon_init();
    recon_core_reset();

    esp_wifi_set_promiscuous_rx_cb(promisc_cb);
    if (esp_wifi_set_promiscuous(true) != ESP_OK) {
        ESP_LOGE(TAG, "failed to enter promiscuous mode");
        return false;
    }
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&(wifi_promiscuous_filter_t){
        .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL}));
    s_enabled = true;
    ESP_LOGI(TAG, "sniffing on");
    return true;
}

void recon_stop(void) {
    if (!s_enabled) {
        return;
    }
    s_enabled = false;
    esp_wifi_set_promiscuous(false);
    /* keep our AP usable for the phone */
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    ESP_LOGI(TAG, "sniffing off, AP back on ch1");
}

bool recon_running(void) { return s_enabled; }

void recon_hop_once(void) {
    for (int ch = 1; ch <= 13; ch++) {
        esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

void recon_lock(void) { xSemaphoreTake(s_mux, portMAX_DELAY); }
void recon_unlock(void) { xSemaphoreGive(s_mux); }

bool recon_get_ap(const uint8_t bssid[6], recon_ap_t *out) {
    if (!out) {
        return false;
    }
    recon_lock();
    uint32_t i = recon_core_ap_find(bssid);
    bool ok = false;
    if (i != UINT32_MAX) {
        *out = *recon_core_ap_get(i);
        ok = true;
    }
    recon_unlock();
    return ok;
}