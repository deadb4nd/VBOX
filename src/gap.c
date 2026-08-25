#include "gap.h"
#include "common.h"
#include <stdint.h>
#include <string.h>

/* Private function declarations */
inline static void format_addr(char *addr_str, uint8_t addr[]);
static void start_advertising(void);

/* Private variables */
static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};

// A list of names to spam. You can add anything you want here!
static const char *spam_names[] = {"iPhone 15 Pro",   "Tesla Model 3",
                                   "Sony WH-1000XM4", "Samsung Smart Fridge",
                                   "Flippers Zero",   "AirPods Pro"};
#define NUM_SPAM_NAMES (sizeof(spam_names) / sizeof(spam_names[0]))
static size_t current_name_index = 0;

/* Private functions */
inline static void format_addr(char *addr_str, uint8_t addr[]) {
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
            addr[2], addr[3], addr[4], addr[5]);
}

// Event handler required to catch when the brief advertisement window finishes
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        // The 100ms window finished! Cycle to the next name and fire again.
        current_name_index = (current_name_index + 1) % NUM_SPAM_NAMES;
        start_advertising();
    }
    return 0;
}

static void start_advertising(void) {
    int rc = 0;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_gap_adv_params adv_params = {0};

    /* Set advertising flags */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* DYNAMIC SPAM: Fetch the current name from our cycling list */
    const char *name = spam_names[current_name_index];
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    /* Set device metadata flags */
    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_TAG;
    adv_fields.appearance_is_present = 1;
    adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
    adv_fields.le_role_is_present = 1;

    /* Update active fields in the NimBLE Host */
    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
        return;
    }

    /* Set connection configurations */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    /*
     * CRITICAL CHANGE: Instead of BLE_HS_FOREVER, we advertise for 100ms
     * duration. When 100ms finishes, it will trigger the event handler above to
     * swap names.
     */
    rc = ble_gap_adv_start(own_addr_type, NULL, 100, &adv_params,
                           ble_gap_event_handler, NULL);
    if (rc != 0) {
        // If it throws an error because the radio is busy, retry in the next
        // cycle
        return;
    }
}

/* Public functions */
void adv_init(void) {
    int rc = 0;
    char addr_str[18] = {0};

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "device does not have any available bt address!");
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to infer address type, error code: %d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to copy device address, error code: %d", rc);
        return;
    }
    format_addr(addr_str, addr_val);
    ESP_LOGI(TAG, "device physical address: %s", addr_str);

    /* Kick off the self-repeating spam chain */
    start_advertising();
}

int gap_init(void) {
    int rc = 0;
    ble_svc_gap_init();

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set device name, error code: %d", rc);
        return rc;
    }

    rc = ble_svc_gap_device_appearance_set(BLE_GAP_APPEARANCE_GENERIC_TAG);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set device appearance, error code: %d", rc);
        return rc;
    }
    return rc;
}
