#pragma once

#include "settings.h"

#if defined(ESP_PLATFORM)
#include "esp_err.h"

esp_err_t settings_nvs_save(const velo_settings_t *s);
esp_err_t settings_nvs_load(velo_settings_t *s);
#endif