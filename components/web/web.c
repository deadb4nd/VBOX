#include "web.h"

#include "actions.h"
#include "ble_spam.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "fakeap.h"
#include "harvest.h"
#include "power.h"
#include "recon.h"
#include "script.h"
#include "settings.h"
#include "settings_nvs.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SPIFFS_MOUNT "/spiffs"
#define SPIFFS_PARTITION "storage"
#define SERVER_PORT 80
#define SCRIPT_PATH SPIFFS_MOUNT "/script.txt"

static const char *DEFAULT_SCRIPT =
    "# VeloBox script - one step per line\n"
    "# commands: deauth recon blescan blespam probe fakeap wait\n"
    "# args: ap=MAC client=MAC ms=N  (bare MAC and bare number also work)\n"
    "recon 15000\n"
    "deauth 5000\n"
    "wait 1000\n";

static const char *TAG = "WEB";

static velo_settings_t *g_settings = NULL;
static httpd_handle_t g_server = NULL;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static esp_err_t httpd_send_json(httpd_req_t *req, int status,
                                 const char *json) {
    httpd_resp_set_status(req, status == 200 ? "200 OK" : "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static void send_ok(httpd_req_t *req) { httpd_send_json(req, 200, "{\"ok\":true}"); }

static void send_err(httpd_req_t *req, const char *err) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", err);
    httpd_send_json(req, 400, buf);
}

/* URL-decode a value (also converts '+' to space). */
static size_t url_decode(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    unsigned hi, lo;
    while (*src && i + 1 < cap) {
        char c = *src++;
        if (c == '%' && src[0] && src[1]) {
            hi = (unsigned char)src[0];
            lo = (unsigned char)src[1];
            if (hi >= '0' && hi <= '9') hi -= '0';
            else if (hi >= 'a' && hi <= 'f') hi -= 'a' - 10;
            else if (hi >= 'A' && hi <= 'F') hi -= 'A' - 10;
            else { dst[i++] = c; continue; }
            if (lo >= '0' && lo <= '9') lo -= '0';
            else if (lo >= 'a' && lo <= 'f') lo -= 'a' - 10;
            else if (lo >= 'A' && lo <= 'F') lo -= 'A' - 10;
            else { dst[i++] = c; continue; }
            dst[i++] = (char)((hi << 4) | lo);
            src += 2;
        } else if (c == '+') {
            dst[i++] = ' ';
        } else {
            dst[i++] = c;
        }
    }
    dst[i] = '\0';
    return i;
}

/* Pull a key from a form-style body ("a=1&b=hello"). Returns false if
   the key is missing. The value is URL-decoded into `out`. */
static bool form_value(const char *body, const char *key, char *out,
                       size_t out_cap) {
    size_t klen = strlen(key);
    const char *p = body;
    while (p) {
        const char *amp = strchr(p, '&');
        size_t seg_len = amp ? (size_t)(amp - p) : strlen(p);
        if (seg_len > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val = p + klen + 1;
            size_t vlen = amp ? (size_t)(amp - val) : strlen(val);
            char tmp[512];
            if (vlen + 1 > sizeof(tmp)) vlen = sizeof(tmp) - 1;
            memcpy(tmp, val, vlen);
            tmp[vlen] = '\0';
            url_decode(out, out_cap, tmp);
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

static bool as_bool(const char *v) {
    return strcmp(v, "1") == 0 || strcmp(v, "true") == 0 ||
           strcmp(v, "on") == 0 || strcmp(v, "yes") == 0;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a 12-char hex string (no separators) into a 6-byte MAC.
   Returns false on invalid input or empty string. */
static bool parse_hex6(const char *in, uint8_t out[6]) {
    if (!in || strlen(in) != 12) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        int hi = hex_nibble(in[i * 2]);
        int lo = hex_nibble(in[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/* Minimal JSON string escape; truncates at `len` incl. NUL. */
static size_t json_escape(char *out, size_t len, const char *in) {
    size_t i = 0;
    while (*in && i + 7 < len) {
        unsigned char c = (unsigned char)*in++;
        switch (c) {
        case '"':
            out[i++] = '\\';
            out[i++] = '"';
            break;
        case '\\':
            out[i++] = '\\';
            out[i++] = '\\';
            break;
        case '\n':
            out[i++] = '\\';
            out[i++] = 'n';
            break;
        case '\r':
            out[i++] = '\\';
            out[i++] = 'r';
            break;
        case '\t':
            out[i++] = '\\';
            out[i++] = 't';
            break;
        default:
            if (c >= 0x20) out[i++] = (char)c;
        }
    }
    out[i] = '\0';
    return i;
}

static const char *action_slug(action_t a) {
    switch (a) {
    case ACTION_WIFI_DEAUTH:
        return "deauth";
    case ACTION_BLE_SPAM:
        return "ble";
    case ACTION_FAKE_AP:
        return "fakeap";
    case ACTION_RECON:
        return "recon";
    case ACTION_BLE_SCAN:
        return "blescan";
    case ACTION_PROBE_FLOOD:
        return "probe";
    default:
        return "none";
    }
}

static const char *content_type_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html") || !strcmp(dot, ".htm")) return "text/html";
    if (!strcmp(dot, ".css")) return "text/css";
    if (!strcmp(dot, ".js")) return "application/javascript";
    if (!strcmp(dot, ".json") || !strcmp(dot, ".map")) return "application/json";
    if (!strcmp(dot, ".png")) return "image/png";
    if (!strcmp(dot, ".svg")) return "image/svg+xml";
    if (!strcmp(dot, ".ico")) return "image/x-icon";
    if (!strcmp(dot, ".txt")) return "text/plain";
    return "application/octet-stream";
}

static esp_err_t serve_file(httpd_req_t *req, const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type_for(path));
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char buf[512];
    ssize_t n;
    esp_err_t err = ESP_OK;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (httpd_resp_send_chunk(req, buf, (size_t)n) != ESP_OK) {
            err = ESP_FAIL;
            break;
        }
    }
    close(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t cap) {
    size_t total = req->content_len;
    if (total >= cap) total = cap - 1;
    size_t got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, buf + got, total - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    buf[got] = '\0';
    return ESP_OK;
}

/* Any HTTP request counts as activity so the box does not deep-sleep while
   someone is poking the control panel. */
static void note_activity(void) { power_note_activity(esp_timer_get_time()); }

static size_t read_file(const char *path, char *buf, size_t cap) {
    if (!buf || cap == 0) return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        buf[0] = '\0';
        return 0;
    }
    ssize_t n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';
    return (size_t)n;
}

static bool write_file(const char *path, const char *data) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    size_t len = strlen(data);
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n <= 0) {
            close(fd);
            return false;
        }
        off += (size_t)n;
    }
    close(fd);
    return true;
}

/* ------------------------------------------------------------------ */
/*  URI handlers                                                      */
/* ------------------------------------------------------------------ */

static esp_err_t handler_root(httpd_req_t *req) {
    return serve_file(req, SPIFFS_MOUNT "/index.html");
}

static esp_err_t handler_static(httpd_req_t *req) {
    /* Reject path traversal and non-GET. */
    if (strstr(req->uri, "..") != NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }

    char path[288 + sizeof(SPIFFS_MOUNT)];
    const char *uri = req->uri;
    int len = strlen(uri);
    if (len > 0 && uri[len - 1] == '/') {
        len--; /* strip trailing slash */
    }
    if (len <= 0) {
        return handler_root(req);
    }
    if (len + 1 + (int)strlen(SPIFFS_MOUNT) >= (int)sizeof(path)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }
    size_t mlen = strlen(SPIFFS_MOUNT);
    memcpy(path, SPIFFS_MOUNT, mlen);
    memcpy(path + mlen, uri, (size_t)len + 1);
    return serve_file(req, path);
}

static void mac_to_hex(char *out, const uint8_t m[6]);

static esp_err_t handler_api_status(httpd_req_t *req) {
    note_activity();
    bool running = actions_is_running();
    action_t cur = actions_current();
    bool idle = !running && g_settings &&
                g_settings->default_action == SETTING_ACTION_OFF;

    uint32_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    action_counters_t ctr = actions_counters();
    recon_lock();
    uint32_t handshakes = harvest_ready_count();
    uint32_t frames = harvest_frame_count();
    recon_unlock();

    size_t cap = 2048;
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_8BIT);
    if (!buf) {
        send_err(req, "oom");
        return ESP_OK;
    }
    char *p = buf;
    char *end = buf + cap;
    int n = snprintf(p, (size_t)(end - p),
             "{\"running\":%s,\"idle\":%s,\"action\":\"%s\",\"label\":\"%s\","
             "\"deauth\":%lu,\"probes\":%lu,\"beacons\":%lu,"
             "\"handshakes\":%lu,\"eapol_frames\":%lu,\"free_heap\":%lu",
             running ? "true" : "false", idle ? "true" : "false",
             action_slug(cur), actions_name(cur), (unsigned long)ctr.deauth_sent,
             (unsigned long)ctr.probe_sent, (unsigned long)ctr.beacon_sent,
             (unsigned long)handshakes, (unsigned long)frames,
             (unsigned long)free);
    p += n;

    script_t sc;
    uint32_t idx = 0;
    bool script_on = actions_script_snapshot(&sc, &idx);
    n = snprintf(p, (size_t)(end - p),
                 ",\"script\":{\"running\":%s,\"index\":%lu,\"count\":%lu,"
                 "\"steps\":[",
                 script_on ? "true" : "false",
                 (unsigned long)(script_on ? idx : 0),
                 (unsigned long)(script_on ? sc.count : 0));
    p += n;
    if (script_on) {
        for (uint32_t i = 0; i < sc.count && p < end - 128; i++) {
            const script_step_t *st = &sc.steps[i];
            char ap[18] = "";
            char sta[18] = "";
            if (st->has_ap) {
                mac_to_hex(ap, st->ap);
            }
            if (st->has_client) {
                mac_to_hex(sta, st->client);
            }
            const char *phase =
                i < idx ? "done" : (i == idx ? "active" : "pending");
            n = snprintf(p, (size_t)(end - p),
                         "%s{\"cmd\":\"%s\",\"ms\":%lu,\"ap\":\"%s\","
                         "\"sta\":\"%s\",\"phase\":\"%s\"}",
                         i ? "," : "", script_cmd_name(st->cmd),
                         (unsigned long)st->duration_ms, ap, sta, phase);
            if (n < 0) {
                break;
            }
            p += n;
        }
    }
    snprintf(p, (size_t)(end - p), "]}}\n");
    esp_err_t err = httpd_send_json(req, 200, buf);
    heap_caps_free(buf);
    return err;
}

static esp_err_t handler_api_action(httpd_req_t *req) {
    note_activity();
    if (req->content_len <= 0) {
        send_err(req, "missing body");
        return ESP_OK;
    }
    char body[128];
    read_body(req, body, sizeof(body));

    char name[32];
    if (!form_value(body, "name", name, sizeof(name))) {
        send_err(req, "missing name");
        return ESP_OK;
    }

    action_t a;
    if (!strcmp(name, "deauth")) {
        a = ACTION_WIFI_DEAUTH;
    } else if (!strcmp(name, "ble")) {
        a = ACTION_BLE_SPAM;
    } else if (!strcmp(name, "fakeap")) {
        a = ACTION_FAKE_AP;
    } else if (!strcmp(name, "recon")) {
        a = ACTION_RECON;
    } else if (!strcmp(name, "blescan")) {
        a = ACTION_BLE_SCAN;
    } else if (!strcmp(name, "probe")) {
        a = ACTION_PROBE_FLOOD;
    } else {
        send_err(req, "unknown action");
        return ESP_OK;
    }

    /* optional targeting for deauth */
    actions_clear_target();
    uint8_t ap[6], client[6];
    char v[16];
    if (form_value(body, "bssid", v, sizeof(v)) && parse_hex6(v, ap)) {
        if (form_value(body, "client", v, sizeof(v)) && parse_hex6(v, client)) {
            actions_set_target(ap, client);
        } else {
            actions_set_target(ap, NULL);
        }
    }

    if (actions_is_running()) {
        send_err(req, "already running");
        return ESP_OK;
    }
    if (!actions_start(a)) {
        send_err(req, "action declined");
        return ESP_OK;
    }
    send_ok(req);
    return ESP_OK;
}

static esp_err_t handler_api_stop(httpd_req_t *req) {
    actions_stop();
    send_ok(req);
    return ESP_OK;
}

static esp_err_t handler_api_settings_get(httpd_req_t *req) {
    note_activity();
    if (!g_settings) {
        send_err(req, "settings unavailable");
        return ESP_OK;
    }
    char ssids_buf[SETTINGS_MAX_SSIDS * SETTINGS_SSID_MAX_LEN];
    settings_ssids_to_text(g_settings, ssids_buf, sizeof(ssids_buf));
    char ssids_json[SETTINGS_MAX_SSIDS * SETTINGS_SSID_MAX_LEN * 2 + 8];
    json_escape(ssids_json, sizeof(ssids_json), ssids_buf);

    char ap_json[SETTINGS_SSID_MAX_LEN * 2 + 8];
    json_escape(ap_json, sizeof(ap_json), g_settings->ap_ssid);

    char buf[768 + sizeof(ssids_json)];
    int n = snprintf(
        buf, sizeof(buf),
        "{\"default_action\":%d,\"idle_timeout_ms\":%u,"
        "\"warning_duration_ms\":%u,\"ap_ssid\":\"%s\","
        "\"fakeap_channel\":%u,"
        "\"fakeap_max_connections\":%u,\"fakeap_beacon_interval\":%u,"
        "\"ble_spam_enabled\":%s,\"sleep_timeout_ms\":%lu,\"ssids\":\"%s\"}\n",
        (int)g_settings->default_action, g_settings->idle_timeout_ms,
        g_settings->warning_duration_ms, ap_json, g_settings->fakeap_channel,
        g_settings->fakeap_max_connections, g_settings->fakeap_beacon_interval,
        g_settings->ble_spam_enabled ? "true" : "false",
        (unsigned long)g_settings->sleep_timeout_ms, ssids_json);
    if (n < 0) {
        send_err(req, "encoding failed");
        return ESP_OK;
    }
    return httpd_send_json(req, 200, buf);
}

static esp_err_t handler_api_settings_post(httpd_req_t *req) {
    note_activity();
    if (!g_settings) {
        send_err(req, "settings unavailable");
        return ESP_OK;
    }
    char body[1536];
    read_body(req, body, sizeof(body));

    char v[48];
    char ssids[SETTINGS_MAX_SSIDS * SETTINGS_SSID_MAX_LEN];

    if (form_value(body, "default_action", v, sizeof(v))) {
        settings_set_default_action(g_settings, (settings_action_t)atoi(v));
    }
    if (form_value(body, "idle_timeout_ms", v, sizeof(v))) {
        settings_set_idle_timeout_ms(g_settings, (uint16_t)atoi(v));
    }
    if (form_value(body, "warning_duration_ms", v, sizeof(v))) {
        settings_set_warning_duration_ms(g_settings, (uint16_t)atoi(v));
    }
    if (form_value(body, "ap_ssid", v, sizeof(v))) {
        settings_set_ap_ssid(g_settings, v);
    }
    if (form_value(body, "fakeap_channel", v, sizeof(v))) {
        settings_set_fakeap_channel(g_settings, (uint8_t)atoi(v));
    }
    if (form_value(body, "fakeap_max_connections", v, sizeof(v))) {
        settings_set_fakeap_max_connections(g_settings, (uint8_t)atoi(v));
    }
    if (form_value(body, "fakeap_beacon_interval", v, sizeof(v))) {
        settings_set_fakeap_beacon_interval(g_settings, (uint16_t)atoi(v));
    }
    if (form_value(body, "ble_spam_enabled", v, sizeof(v))) {
        settings_set_ble_spam_enabled(g_settings, as_bool(v));
    }
    if (form_value(body, "sleep_timeout_ms", v, sizeof(v))) {
        settings_set_sleep_timeout_ms(g_settings, (uint32_t)strtoul(v, NULL, 10));
    }
    if (form_value(body, "ssids", ssids, sizeof(ssids))) {
        settings_set_ssids_text(g_settings, ssids);
    }

    settings_nvs_save(g_settings);
    ap_set_name(g_settings->ap_ssid); /* re-broadcast AP name live */
    ESP_LOGI(TAG, "settings saved: action=%s idle=%ums warning=%ums "
                  "channel=%u conn=%u tu=%u ble=%s sleep=%lums ssids=%u ap=%s",
             actions_name((action_t)((int)g_settings->default_action + 1)),
             g_settings->idle_timeout_ms, g_settings->warning_duration_ms,
             g_settings->fakeap_channel, g_settings->fakeap_max_connections,
             g_settings->fakeap_beacon_interval,
             g_settings->ble_spam_enabled ? "yes" : "no",
             (unsigned long)g_settings->sleep_timeout_ms,
             g_settings->ssid_count, g_settings->ap_ssid);
    send_ok(req);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Recon / scan API                                                  */
/* ------------------------------------------------------------------ */

static void mac_to_hex(char *out, const uint8_t m[6]) {
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2],
             m[3], m[4], m[5]);
}

static void bytes_to_hex(char *out, const uint8_t *b, size_t n) {
    out[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        snprintf(out + i * 2, 3, "%02x", b[i]);
    }
}

static esp_err_t handler_api_scan(httpd_req_t *req) {
    /* JSON is built into a heap buffer; device RAM is fine for a page
       of tables but the buffer is capped so a giant sky full of APs
       degrades gracefully. */
    size_t cap = 8192;
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_8BIT);
    if (!buf) {
        send_err(req, "oom");
        return ESP_OK;
    }
    char *p = buf;
    char *end = buf + cap;

    bool recon_on = actions_current() == ACTION_RECON && actions_is_running();
    bool ble_on = actions_current() == ACTION_BLE_SCAN && actions_is_running();

    int n = snprintf(p, (size_t)(end - p),
                     "{\"running\":%s,\"ble_running\":%s,"
                     "\"aps\":[",
                     recon_on ? "true" : "false",
                     ble_on ? "true" : "false");
    p += n;

    uint32_t comma = 0;
    uint32_t ap_n = 0, st_n = 0, ble_n = 0;

    recon_lock();
    ap_n = recon_core_ap_count();
    for (uint32_t i = 0; i < ap_n; i++) {
        if (p >= end - 220) break;
        const recon_ap_t *ap = recon_core_ap_get(i);
        char mac[18], ssid[RECON_SSID_LEN * 2 + 2];
        mac_to_hex(mac, ap->bssid);
        json_escape(ssid, sizeof(ssid), ap->ssid);
        n = snprintf(p, (size_t)(end - p), "%s{\"bssid\":\"%s\",\"ssid\":\"%s\","
                     "\"ch\":%u,\"rssi\":%d,\"auth\":%u,\"hidden\":%s}",
                     comma++ ? "," : "", mac, ssid, ap->channel, ap->rssi,
                     ap->authmode, ap->hidden ? "true" : "false");
        if (n < 0 || p + n >= end) break;
        p += n;
    }
    recon_unlock();

    n = snprintf(p, (size_t)(end - p), "],\"stations\":[");
    p += n;
    comma = 0;

    recon_lock();
    st_n = recon_core_station_count();
    for (uint32_t i = 0; i < st_n; i++) {
        if (p >= end - 160) break;
        const recon_station_t *st = recon_core_station_get(i);
        char mac[18], ap[18];
        mac_to_hex(mac, st->mac);
        mac_to_hex(ap, st->ap);
        n = snprintf(p, (size_t)(end - p), "%s{\"mac\":\"%s\",\"ap\":\"%s\","
                     "\"rssi\":%d}", comma++ ? "," : "", mac, ap, st->rssi);
        if (n < 0 || p + n >= end) break;
        p += n;
    }
    recon_unlock();

    recon_lock();
    const recon_counters_t *ctr = recon_core_counters();
    uint32_t ready_n = harvest_ready_count();
    uint32_t pair_n = harvest_pair_count();
    uint32_t frame_n = harvest_frame_count();
    n = snprintf(p, (size_t)(end - p),
                 "],\"counters\":{\"beacons\":%lu,\"probe_reqs\":%lu,"
                 "\"probe_resps\":%lu,\"deauths\":%lu,\"eapol\":%lu,"
                 "\"data\":%lu},"
                 "\"harvest\":{\"ready\":%lu,\"pairs\":%lu,"
                 "\"eapol_frames\":%lu},\"captures\":[",
                 (unsigned long)ctr->beacons, (unsigned long)ctr->probe_reqs,
                 (unsigned long)ctr->probe_resps, (unsigned long)ctr->deauths,
                 (unsigned long)ctr->eapol, (unsigned long)ctr->data,
                 (unsigned long)ready_n, (unsigned long)pair_n,
                 (unsigned long)frame_n);
    recon_unlock();
    p += n;
    comma = 0;

    recon_lock();
    for (uint32_t i = 0; i < pair_n && p < end - 200; i++) {
        harvest_pair_t hp;
        if (!harvest_pair_get(i, &hp)) {
            break;
        }
        char hmac[18], hmac_sta[18], pmkid[33] = "";
        mac_to_hex(hmac, hp.ap);
        mac_to_hex(hmac_sta, hp.sta);
        bytes_to_hex(pmkid, hp.pmkid, 16);
        n = snprintf(p, (size_t)(end - p), "%s{\"ap\":\"%s\",\"sta\":\"%s\","
                     "\"msgs\":%u,\"ready\":%s,\"has_pmkid\":%s,"
                     "\"pmkid\":\"%s\"}",
                     comma++ ? "," : "", hmac, hmac_sta, hp.msgs,
                     harvest_pair_ready(&hp) ? "true" : "false",
                     hp.has_pmkid ? "true" : "false",
                     hp.has_pmkid ? pmkid : "");
        if (n < 0 || p + n >= end) break;
        p += n;
    }
    recon_unlock();

    ble_n = ble_scan_count();
    for (uint32_t i = 0; i < ble_n && p < end - 140; i++) {
        uint8_t mac[6];
        char name[BLE_SCAN_NAME_LEN], macs[18];
        int8_t rssi = 0;
        ble_scan_get(i, mac, name, &rssi);
        mac_to_hex(macs, mac);
        char name_json[BLE_SCAN_NAME_LEN * 2 + 2];
        json_escape(name_json, sizeof(name_json), name);
        n = snprintf(p, (size_t)(end - p), "%s{\"mac\":\"%s\",\"name\":\"%s\","
                     "\"rssi\":%d}", comma++ ? "," : "", macs, name_json,
                     rssi);
        if (n < 0 || p + n >= end) break;
        p += n;
    }

    n = snprintf(p, (size_t)(end - p), "]}\n");
    p += n;

    httpd_send_json(req, 200, buf);
    heap_caps_free(buf);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Handshake harvest API                                             */
/* ------------------------------------------------------------------ */

static esp_err_t handler_api_captures(httpd_req_t *req) {
    char buf[2048];
    char *p = buf;
    char *end = buf + sizeof(buf);

    recon_lock();
    uint32_t ready_n = harvest_ready_count();
    uint32_t pair_n = harvest_pair_count();
    int n = snprintf(p, (size_t)(end - p), "{\"ready\":%lu,\"pairs\":[",
                     (unsigned long)ready_n);
    p += n;
    uint32_t comma = 0;
    for (uint32_t i = 0; i < pair_n && p < end - 200; i++) {
        harvest_pair_t hp;
        if (!harvest_pair_get(i, &hp)) break;
        char hmac[18], hmsta[18], pmkid[33] = "";
        mac_to_hex(hmac, hp.ap);
        mac_to_hex(hmsta, hp.sta);
        bytes_to_hex(pmkid, hp.pmkid, 16);
        n = snprintf(p, (size_t)(end - p), "%s{\"ap\":\"%s\",\"sta\":\"%s\","
                     "\"msgs\":%u,\"ready\":%s,\"has_pmkid\":%s,"
                     "\"pmkid\":\"%s\"}",
                     comma++ ? "," : "", hmac, hmsta, hp.msgs,
                     harvest_pair_ready(&hp) ? "true" : "false",
                     hp.has_pmkid ? "true" : "false",
                     hp.has_pmkid ? pmkid : "");
        if (n < 0 || p + n >= end) break;
        p += n;
    }
    recon_unlock();
    n = snprintf(p, (size_t)(end - p), "]}\n");
    p += n;
    return httpd_send_json(req, 200, buf);
}

static esp_err_t handler_api_captures_download(httpd_req_t *req) {
    recon_lock();
    size_t sz = harvest_pcap_size();
    uint8_t *buf = sz ? heap_caps_malloc(sz, MALLOC_CAP_8BIT) : NULL;
    size_t wrote = 0;
    if (buf) {
        wrote = harvest_build_pcap(buf, sz);
    }
    recon_unlock();

    if (!buf) {
        send_err(req, "nothing captured yet");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/vnd.tcpdump.pcap");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"capture.pcap\"");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t err = httpd_resp_send(req, (const char *)buf, wrote);
    heap_caps_free(buf);
    return err;
}

static esp_err_t handler_api_captures_clear(httpd_req_t *req) {
    recon_lock();
    harvest_reset();
    recon_unlock();
    send_ok(req);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Script API                                                        */
/* ------------------------------------------------------------------ */

/* GET /api/script -> {"text":"...","steps":N,"running":bool} */
static esp_err_t handler_api_script_get(httpd_req_t *req) {
    note_activity();
    char text[SCRIPT_MAX_TEXT];
    read_file(SCRIPT_PATH, text, sizeof(text));

    script_t sc;
    uint32_t steps = script_parse(text, &sc);
    size_t cap = strlen(text) * 2 + 96;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_8BIT);
    if (!out) {
        send_err(req, "oom");
        return ESP_OK;
    }
    int w = snprintf(out, cap, "{\"steps\":%lu,\"running\":%s,\"text\":\"",
                     (unsigned long)steps,
                     actions_script_running() ? "true" : "false");
    w += (int)json_escape(out + w, cap - (size_t)w, text);
    if ((size_t)w + 3 < cap) {
        out[w++] = '"';
        out[w++] = '}';
        out[w++] = '\n';
        out[w] = '\0';
    }
    esp_err_t err = httpd_send_json(req, 200, out);
    heap_caps_free(out);
    return err;
}

/* POST /api/script  (raw body = script text) -> save to SPIFFS */
static esp_err_t handler_api_script_post(httpd_req_t *req) {
    note_activity();
    char text[SCRIPT_MAX_TEXT];
    read_body(req, text, sizeof(text));
    if (!write_file(SCRIPT_PATH, text)) {
        send_err(req, "write failed");
        return ESP_OK;
    }
    script_t sc;
    uint32_t steps = script_parse(text, &sc);
    char out[64];
    snprintf(out, sizeof(out), "{\"ok\":true,\"steps\":%lu}\n",
             (unsigned long)steps);
    return httpd_send_json(req, 200, out);
}

/* POST /api/script/run  (optional raw body = script text, else saved file) */
static esp_err_t handler_api_script_run(httpd_req_t *req) {
    note_activity();
    if (actions_is_running()) {
        send_err(req, "already running");
        return ESP_OK;
    }
    char text[SCRIPT_MAX_TEXT];
    if (req->content_len > 0) {
        read_body(req, text, sizeof(text));
    } else if (read_file(SCRIPT_PATH, text, sizeof(text)) == 0) {
        send_err(req, "no script");
        return ESP_OK;
    }

    script_t sc;
    uint32_t steps = script_parse(text, &sc);
    if (steps == 0) {
        send_err(req, "empty script");
        return ESP_OK;
    }
    if (!actions_start_script(&sc)) {
        send_err(req, "start failed");
        return ESP_OK;
    }
    char out[64];
    snprintf(out, sizeof(out), "{\"ok\":true,\"steps\":%lu}\n",
             (unsigned long)steps);
    return httpd_send_json(req, 200, out);
}

/* ------------------------------------------------------------------ */
/*  Init                                                              */
/* ------------------------------------------------------------------ */

static esp_err_t spiffs_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_MOUNT,
        .partition_label = SPIFFS_PARTITION,
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_spiffs_info(SPIFFS_PARTITION, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %u/%u bytes used", (unsigned)used,
                 (unsigned)total);
    }
    return ESP_OK;
}

static void register_handler(httpd_handle_t server, const char *uri,
                             httpd_method_t method, esp_err_t (*fn)(httpd_req_t *)) {
    httpd_uri_t h = {.uri = uri, .method = method, .handler = fn, .user_ctx = NULL};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &h));
}

esp_err_t web_start(velo_settings_t *s) {
    g_settings = s;

    esp_err_t err = spiffs_init();
    if (err != ESP_OK) {
        return err;
    }

    /* seed the script editor with a working example on first boot */
    char seed[SCRIPT_MAX_TEXT];
    if (read_file(SCRIPT_PATH, seed, sizeof(seed)) == 0) {
        write_file(SCRIPT_PATH, DEFAULT_SCRIPT);
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = SERVER_PORT;
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 16;
    /* Required: default matcher is exact strcmp only, so the wildcard
       static handler would never match. Enable wildcard URIs. */
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    err = httpd_start(&g_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed: %s", esp_err_to_name(err));
        return err;
    }

    register_handler(g_server, "/", HTTP_GET, handler_root);
    register_handler(g_server, "/api/status", HTTP_GET, handler_api_status);
    register_handler(g_server, "/api/action", HTTP_POST, handler_api_action);
    register_handler(g_server, "/api/stop", HTTP_POST, handler_api_stop);
    register_handler(g_server, "/api/settings", HTTP_GET, handler_api_settings_get);
    register_handler(g_server, "/api/settings", HTTP_POST, handler_api_settings_post);
    register_handler(g_server, "/api/scan", HTTP_GET, handler_api_scan);
    register_handler(g_server, "/api/captures", HTTP_GET, handler_api_captures);
    register_handler(g_server, "/api/captures/download", HTTP_GET,
                     handler_api_captures_download);
    register_handler(g_server, "/api/captures/clear", HTTP_POST,
                     handler_api_captures_clear);
    register_handler(g_server, "/api/script", HTTP_GET, handler_api_script_get);
    register_handler(g_server, "/api/script", HTTP_POST, handler_api_script_post);
    register_handler(g_server, "/api/script/run", HTTP_POST,
                     handler_api_script_run);
    /* wildcard LAST: esp_http_server matches in registration order, so
       the wildcard must come after every exact route or it swallows them */
    register_handler(g_server, "/*", HTTP_GET, handler_static);

    ESP_LOGI(TAG, "control panel online at http://192.168.4.1/");
    return ESP_OK;
}