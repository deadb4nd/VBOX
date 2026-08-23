#include "ap_config.h"
#include <string.h>

void create_config(ap_config_t *config) {
    if (config->WIFI_CHANNEL == 0) {
        config->WIFI_CHANNEL = 2;
    }
    if (config->MAX_CONNECTIONS == 0) {
        config->MAX_CONNECTIONS = 4;
    }
    if (config->SSID[0] == '\0') {
        strncpy((char *)config->SSID, "ESP32C6 WiFi", sizeof(config->SSID) - 1);
        config->SSID[sizeof(config->SSID) - 1] = '\0';
    }
    if (config->PASSWORD[0] == '\0') {
        memset(config->PASSWORD, 0, sizeof(config->PASSWORD));
    }
}
