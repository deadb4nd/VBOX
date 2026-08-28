#ifndef _AP_CONFIG_H_
#define _AP_CONFIG_H_
#include <stdint.h>

#define AP_PASSWORD_MAX_LEN 64
typedef struct {
    uint8_t SSID[32];
    uint8_t PASSWORD[AP_PASSWORD_MAX_LEN];
    uint8_t WIFI_CHANNEL;
    uint8_t MAX_CONNECTIONS;
} ap_config_t;

void create_config(ap_config_t *config);

#endif // !_AP_CONFIG_H_
