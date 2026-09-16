#pragma once

#include "esp_err.h"
#include "settings.h"

/* Starts SPIFFS and the HTTP control-panel server. `s` must outlive the
   server (usually a static settings struct in app_main). */
esp_err_t web_start(velo_settings_t *s);