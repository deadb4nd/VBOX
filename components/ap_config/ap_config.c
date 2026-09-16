#include "ap_config.h"
#include <string.h>

#define AP_DEFAULT_SSID "ESP32C6 WiFi"
#define AP_DEFAULT_CHANNEL 2
#define AP_DEFAULT_MAX_CONNECTIONS 4

void create_config(ap_config_t *config) {
    if (config->WIFI_CHANNEL == 0) {
        config->WIFI_CHANNEL = AP_DEFAULT_CHANNEL;
    }
    if (config->MAX_CONNECTIONS == 0) {
        config->MAX_CONNECTIONS = AP_DEFAULT_MAX_CONNECTIONS;
    }
    if (config->SSID[0] == '\0') {
        strncpy((char *)config->SSID, AP_DEFAULT_SSID,
                sizeof(config->SSID) - 1);
        config->SSID[sizeof(config->SSID) - 1] = '\0';
    }
    /* PASSWORD is only touched by the caller; a zeroed input stays zeroed. */
}