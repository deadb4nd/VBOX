#include "common.h"
#include "gap.h"
#include <string.h>

/* NimBLE Specific Headers */
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_esp.h"
#include "services/gap/ble_svc_gap.h"

/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);
static void start_custom_ble_advertising(void);
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);

/* Private functions */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg) {
    // Required to catch internal stack state transformations
    return 0;
}

static void start_custom_ble_advertising(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    // 1. Set general discovery flags (tells devices this is a standard BLE
    // signal)
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // 2. Set your custom broadcast name
    char *device_name = "Velo-Spam-Box";
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    // Load payload formatting arrays into the active core memory
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE("BLE_BEACON", "Error writing advertising fields: %d", rc);
        return;
    }

    // 3. Configure hardware timing parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode =
        BLE_GAP_CONN_MODE_NON; // Strict connectionless broadcasting
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // Interval: 160 * 0.625ms = 100ms cycle transmission rates
    adv_params.itvl_min = 160;
    adv_params.itvl_max = 160;

    // 4. Start the transmission engine
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE("BLE_BEACON", "Error starting advertising: %d", rc);
        return;
    }
    ESP_LOGI("BLE_BEACON", "BLE advertising loop is running smoothly.");
}

static void on_stack_reset(int reason) {
    ESP_LOGI("BLE_BEACON", "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) {
    /* Trigger the transmission configuration sequence upon hardware
     * synchronization */
    start_custom_ble_advertising();
}

static void nimble_host_config_init(void) {
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    ESP_LOGI("BLE_BEACON", "nimble host task has been started!");

    // Blocks here running the continuous background transmission thread
    nimble_port_run();

    // Clean up task resources cleanly if an exit is ever called
    vTaskDelete(NULL);
}

void init_ble_beacon(void) {
    int rc = 0;
    esp_err_t ret = ESP_OK;

    // NVS initialization block
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_BEACON", "failed to initialize nvs flash: %d", ret);
        return;
    }

    // Initialize underlying controller configurations
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE("BLE_BEACON", "failed to initialize nimble stack: %d", ret);
        return;
    }

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE("BLE_BEACON", "failed to initialize GAP service: %d", rc);
        return;
    }
#endif

    nimble_host_config_init();

    // Allocate a dedicated system thread to decouple radio scheduling from
    // main.c
    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);
}

// Global cleanup mechanism to safely shut down the radio when your loop
// finishes
void stop_ble_beacon(void) {
    if (nimble_port_stop() == 0) {
        nimble_port_deinit();
        ESP_LOGI("BLE_BEACON", "Radio successfully powered down.");
    }
}
