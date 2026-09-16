#pragma once

/* Pure 802.11 frame parsing + scan tables. No ESP-IDF types on purpose so
   the logic is host-testable. The driver (recon.c) feeds raw frames in.
   All access to these structures must be serialized by the caller (the
   driver holds a mutex around parsing and snapshots). */

#include <stdbool.h>
#include <stdint.h>

#define RECON_MAX_APS 24
#define RECON_MAX_STATIONS 32
#define RECON_SSID_LEN 33

typedef struct {
    uint8_t bssid[6];
    char ssid[RECON_SSID_LEN];
    uint8_t channel;
    int8_t rssi;
    uint8_t authmode; /* RECON_AUTH_* below */
    uint8_t hidden;   /* SSID broadcast suppressed by the AP */
    uint32_t last_seen_ms;
} recon_ap_t;

typedef struct {
    uint8_t mac[6];
    uint8_t ap[6]; /* AP the client talks to; zero if unknown */
    int8_t rssi;
    uint32_t last_seen_ms;
} recon_station_t;

typedef struct {
    uint32_t beacons;
    uint32_t probe_reqs;
    uint32_t probe_resps;
    uint32_t deauths;
    uint32_t eapol;
    uint32_t data;
} recon_counters_t;

typedef enum {
    RECON_AUTH_OPEN = 0,
    RECON_AUTH_WEP = 1,
    RECON_AUTH_WPA = 2,
    RECON_AUTH_WPA2 = 3,
    RECON_AUTH_WPA3 = 4,
    RECON_AUTH_UNKNOWN = 5
} recon_auth_t;

void recon_core_reset(void);

/* Parse one raw 802.11 frame (starting at the MAC header). `len` is the
   full on-air frame length incl. FCS. This is the sniffing hotspot, keep
   it cheap. */
void recon_core_parse(const uint8_t *frame, uint32_t len, uint8_t channel,
                      int8_t rssi, uint32_t now_ms);

uint32_t recon_core_ap_count(void);
const recon_ap_t *recon_core_ap_get(uint32_t i);
uint32_t recon_core_station_count(void);
const recon_station_t *recon_core_station_get(uint32_t i);
const recon_counters_t *recon_core_counters(void);

/* Find an AP by BSSID. Returns index or UINT32_MAX. */
uint32_t recon_core_ap_find(const uint8_t bssid[6]);

/* Copy AP SSID by BSSID if present. False when unknown. */
bool recon_core_ap_ssid(const uint8_t bssid[6], char *out, uint32_t out_cap);

/* Copy up to `n` distinct AP SSIDs into a caller array (min 33 bytes each).
   Used by the fake-AP clone attack. Returns number copied. */
uint32_t recon_core_ssids(char out[][RECON_SSID_LEN], uint32_t n);

/* Copy up to `n` BSSIDs into a caller array. Used by broadcast deauth. */
uint32_t recon_core_aps(uint8_t out[][6], uint32_t n);