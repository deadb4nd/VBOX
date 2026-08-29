#include "ap_config.h"
#include <string.h>

void create_config(ap_config_t *config) {
    if (config->WIFI_CHANNEL == 0) {
        config->WIFI_CHANNEL = 1;
    }
    if (config->MAX_CONNECTIONS == 0) {
        config->MAX_CONNECTIONS = 4;
    }
}
