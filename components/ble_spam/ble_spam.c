#include "ble_spam.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>

static const char *TAG = "BLE_SPAM";

static const char *spam_names[] = {"AirPods Pro",   "AirPods Max",
                                   "Samsung Buds2", "Google Pixel Buds",
                                   "Microsoft",     NULL};

static volatile bool adv_active = false;

static int gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        adv_active = false;
    }
    return 0;
}

static void start_adv(const char *name) {
    struct ble_hs_adv_fields fields = {0};

    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = 0x0020;
    params.itvl_max = 0x0040;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &params,
                           gap_event, NULL);
    if (rc == 0) {
        adv_active = true;
    } else {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    }
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_spam_init(void) {
    ESP_ERROR_CHECK(nimble_port_init());
    nimble_port_freertos_init(ble_host_task);
}

void ble_spam_run_once(void) {
    static int idx = 0;
    const char *name = spam_names[idx];
    if (!name) {
        idx = 0;
        name = spam_names[0];
    }

    ESP_LOGI(TAG, "Spamming as: %s", name);

    if (adv_active) {
        ble_gap_adv_stop();
        adv_active = false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    start_adv(name);
    idx++;
}

void ble_spam_stop(void) {
    if (adv_active) {
        ble_gap_adv_stop();
        adv_active = false;
    }
}
