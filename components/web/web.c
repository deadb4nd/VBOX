#include "web.h"

#include "actions.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "settings.h"
#include "settings_nvs.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SPIFFS_MOUNT "/spiffs"
#define SPIFFS_PARTITION "storage"
#define SERVER_PORT 80

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

static esp_err_t handler_api_status(httpd_req_t *req) {
    bool running = actions_is_running();
    action_t cur = actions_current();

    uint32_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"running\":%s,\"action\":\"%s\",\"label\":\"%s\","
             "\"free_heap\":%lu}\n",
             running ? "true" : "false", action_slug(cur), actions_name(cur),
             (unsigned long)free);
    return httpd_send_json(req, 200, buf);
}

static esp_err_t handler_api_action(httpd_req_t *req) {
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
    } else {
        send_err(req, "unknown action");
        return ESP_OK;
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
    if (!g_settings) {
        send_err(req, "settings unavailable");
        return ESP_OK;
    }
    char ssids_buf[SETTINGS_MAX_SSIDS * SETTINGS_SSID_MAX_LEN];
    settings_ssids_to_text(g_settings, ssids_buf, sizeof(ssids_buf));
    char ssids_json[SETTINGS_MAX_SSIDS * SETTINGS_SSID_MAX_LEN * 2 + 8];
    json_escape(ssids_json, sizeof(ssids_json), ssids_buf);

    char buf[768 + sizeof(ssids_json)];
    int n = snprintf(
        buf, sizeof(buf),
        "{\"default_action\":%d,\"fakeap_channel\":%u,"
        "\"fakeap_max_connections\":%u,\"fakeap_beacon_interval\":%u,"
        "\"ble_spam_enabled\":%s,\"ssids\":\"%s\"}\n",
        (int)g_settings->default_action, g_settings->fakeap_channel,
        g_settings->fakeap_max_connections, g_settings->fakeap_beacon_interval,
        g_settings->ble_spam_enabled ? "true" : "false", ssids_json);
    if (n < 0) {
        send_err(req, "encoding failed");
        return ESP_OK;
    }
    return httpd_send_json(req, 200, buf);
}

static esp_err_t handler_api_settings_post(httpd_req_t *req) {
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
    if (form_value(body, "ssids", ssids, sizeof(ssids))) {
        settings_set_ssids_text(g_settings, ssids);
    }

    settings_nvs_save(g_settings);
    send_ok(req);
    return ESP_OK;
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

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = SERVER_PORT;
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 16;

    err = httpd_start(&g_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed: %s", esp_err_to_name(err));
        return err;
    }

    register_handler(g_server, "/", HTTP_GET, handler_root);
    register_handler(g_server, "/*", HTTP_GET, handler_static);
    register_handler(g_server, "/api/status", HTTP_GET, handler_api_status);
    register_handler(g_server, "/api/action", HTTP_POST, handler_api_action);
    register_handler(g_server, "/api/stop", HTTP_POST, handler_api_stop);
    register_handler(g_server, "/api/settings", HTTP_GET, handler_api_settings_get);
    register_handler(g_server, "/api/settings", HTTP_POST, handler_api_settings_post);

    ESP_LOGI(TAG, "control panel online at http://192.168.4.1/");
    return ESP_OK;
}