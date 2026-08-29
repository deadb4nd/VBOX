#ifndef FAKEAP_H
#define FAKEAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t SSID[32];
    uint8_t PASSWORD[64];
    uint8_t WIFI_CHANNEL;
    uint8_t MAX_CONNECTIONS;
} ap_config_t;

/* your existing globals */
extern volatile bool g_kill_action;
extern char *CUSTOM_SSIDS[];

void init_ap(void);
void ap_run(ap_config_t config, const char *ssid);
void create_config(ap_config_t *config);
int count_ssids(char **ssids);

#ifdef __cplusplus
}
#endif

#endif
