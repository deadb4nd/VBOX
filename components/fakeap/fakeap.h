#ifndef FAKEAP_H
#define FAKEAP_H

#include "ap_config.h"
#include <stdbool.h>
#include <stdint.h>

#define AP_DEFAULT_SSID "VeloBox"
#define AP_DEFAULT_CHANNEL 1
#define AP_DEFAULT_MAX_CONNECTIONS 4

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up softAP + WiFi in AP mode. Safe to call after esp_wifi_stop(). */
void ap_init(void);

/* Set the control-panel WiFi name (SSID). Takes effect now and on every
   future AP (re)start (after fake-AP spam too). Copies the string. */
void ap_set_name(const char *ssid);

/* Re-init AP if it was stopped (e.g. after fake-AP spam). */
void ap_ensure_start(void);

/* Switch the AP radio to a different channel (for raw injection). */
void ap_set_channel(uint8_t channel);

/* Inject a single fake beacon for `ssid` on the current AP channel. */
void ap_run(ap_config_t config, const char *ssid);

#ifdef __cplusplus
}
#endif

#endif