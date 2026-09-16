#include "settings_nvs.h"

#if defined(ESP_PLATFORM)

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "SETTINGS_NVS";
static const char *NVS_KEY = "velobox_cfg";
static const char *NVS_NAMESPACE = "velobox";

esp_err_t settings_nvs_save(const velo_settings_t *s) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open rw failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY, s, sizeof(*s));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "settings saved (%d bytes)", (int)sizeof(*s));
    } else {
        ESP_LOGE(TAG, "settings save failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t settings_nvs_load(velo_settings_t *s) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no stored settings, using defaults (%s)",
                 esp_err_to_name(err));
        settings_init_default(s);
        return ESP_OK;
    }

    size_t len = sizeof(*s);
    err = nvs_get_blob(handle, NVS_KEY, s, &len);
    nvs_close(handle);

    if (err == ESP_OK && len == sizeof(*s)) {
        ESP_LOGI(TAG, "settings loaded");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "stored settings invalid (%s, len=%d), using defaults",
             esp_err_to_name(err), (int)len);
    settings_init_default(s);
    return ESP_OK;
}

#else
/* No-host build: intentionally empty so this file can be linked into
   native unit-test builds without an NVS implementation. */
typedef int settings_nvs_dummy;
#endif