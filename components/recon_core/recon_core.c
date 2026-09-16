#include "recon_core.h"

#include <stdio.h>
#include <string.h>

static recon_ap_t s_aps[RECON_MAX_APS];
static uint32_t s_ap_count = 0;
static recon_station_t s_stations[RECON_MAX_STATIONS];
static uint32_t s_station_count = 0;
static recon_counters_t s_ctr = {0};

static bool mac_zero(const uint8_t m[6]) {
    return m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 0 &&
           m[5] == 0;
}

void recon_core_reset(void) {
    memset(s_aps, 0, sizeof(s_aps));
    memset(s_stations, 0, sizeof(s_stations));
    memset(&s_ctr, 0, sizeof(s_ctr));
    s_ap_count = 0;
    s_station_count = 0;
}

/* ------------------------------------------------------------------ */
/*  Table updating helpers                                            */
/* ------------------------------------------------------------------ */

static uint32_t ap_find_or_add(const uint8_t bssid[6]) {
    for (uint32_t i = 0; i < s_ap_count; i++) {
        if (memcmp(s_aps[i].bssid, bssid, 6) == 0) {
            return i;
        }
    }
    if (s_ap_count < RECON_MAX_APS) {
        uint32_t i = s_ap_count++;
        memcpy(s_aps[i].bssid, bssid, 6);
        s_aps[i].ssid[0] = '\0';
        s_aps[i].channel = 0;
        s_aps[i].rssi = -128;
        s_aps[i].authmode = RECON_AUTH_UNKNOWN;
        s_aps[i].hidden = 0;
        s_aps[i].last_seen_ms = 0;
        return i;
    }
    return UINT32_MAX;
}

static uint32_t station_find_or_add(const uint8_t mac[6]) {
    for (uint32_t i = 0; i < s_station_count; i++) {
        if (memcmp(s_stations[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    if (s_station_count < RECON_MAX_STATIONS) {
        uint32_t i = s_station_count++;
        memcpy(s_stations[i].mac, mac, 6);
        memset(s_stations[i].ap, 0, 6);
        s_stations[i].rssi = -128;
        s_stations[i].last_seen_ms = 0;
        return i;
    }
    return UINT32_MAX;
}

/* ------------------------------------------------------------------ */
/*  Tagged parameter scan                                              */
/* ------------------------------------------------------------------ */

/* Extract SSID / channel / RSN-vendor info from tagged parameters.
   Returns nothing; updates `ap` in place. */
static void parse_ap_tags(recon_ap_t *ap, const uint8_t *f, uint32_t len,
                          uint32_t tags_off) {
    uint32_t pos = tags_off;
    while (pos + 2 <= len) {
        uint32_t id = f[pos];
        uint32_t tlen = f[pos + 1];
        uint32_t start = pos + 2;
        if (start + tlen > len) {
            break;
        }
        switch (id) {
        case 0: /* SSID */
            if (tlen > 0 && tlen < RECON_SSID_LEN && ap->ssid[0] == '\0') {
                memcpy(ap->ssid, &f[start], tlen);
                ap->ssid[tlen] = '\0';
                ap->hidden = 0;
            } else if (tlen == 0) {
                ap->hidden = 1;
            }
            break;
        case 3: /* DS parameter set: channel */
            if (tlen >= 1 && ap->channel == 0) {
                ap->channel = f[start];
            }
            break;
        case 48: /* RSN (WPA2/WPA3) */
            if (tlen >= 2) {
                /* version 1; presence means WPA2+. Check AKM for WPA3. */
                ap->authmode = RECON_AUTH_WPA2;
            }
            break;
        case 221: /* vendor-specific (WPA1 marker) */
            if (tlen >= 4 && f[start] == 0x00 && f[start + 1] == 0x50 &&
                f[start + 2] == 0xf2 && f[start + 3] == 0x01) {
                ap->authmode = RECON_AUTH_WPA;
            }
            break;
        default:
            break;
        }
        pos = start + tlen;
    }
}

/* ------------------------------------------------------------------ */
/*  Frame dispatch                                                     */
/* ------------------------------------------------------------------ */

#define FC_TYPE_OFF 0
#define FC_SUBTYPE(frame) (((frame)[0] >> 4) & 0x0F)
#define FC_TYPE(frame) (((frame)[0] >> 2) & 0x03)
#define FC_TO_DS(frame) ((frame)[1] & 0x01)
#define FC_FROM_DS(frame) ((frame)[1] & 0x02)

#define MGT_BEACON 0x08
#define MGT_PROBE_RESP 0x05
#define MGT_PROBE_REQ 0x04
#define MGT_DEAUTH 0x0C
#define MGT_DISASSOC 0x0A

void recon_core_parse(const uint8_t *frame, uint32_t len, uint8_t channel,
                      int8_t rssi, uint32_t now_ms) {
    if (!frame || len < 24) {
        return; /* too short for a MAC header */
    }

    uint8_t type = FC_TYPE(frame);
    if (type == 0) { /* management */
        uint8_t sub = FC_SUBTYPE(frame);
        switch (sub) {
        case MGT_BEACON: {
            s_ctr.beacons++;
            uint32_t idx = ap_find_or_add(&frame[16]);
            if (idx == UINT32_MAX) {
                return;
            }
            recon_ap_t *ap = &s_aps[idx];
            parse_ap_tags(ap, frame, len, 36);
            ap->channel = channel;
            ap->rssi = rssi;
            ap->last_seen_ms = now_ms;
            break;
        }
        case MGT_PROBE_RESP: {
            s_ctr.probe_resps++;
            uint32_t idx = ap_find_or_add(&frame[16]);
            if (idx == UINT32_MAX) {
                return;
            }
            recon_ap_t *ap = &s_aps[idx];
            parse_ap_tags(ap, frame, len, 36);
            ap->channel = channel;
            ap->rssi = rssi;
            ap->last_seen_ms = now_ms;
            break;
        }
        case MGT_PROBE_REQ: {
            s_ctr.probe_reqs++;
            if (mac_zero(&frame[10])) {
                return;
            }
            uint32_t idx = station_find_or_add(&frame[10]);
            if (idx == UINT32_MAX) {
                return;
            }
            s_stations[idx].rssi = rssi;
            s_stations[idx].last_seen_ms = now_ms;
            break;
        }
        case MGT_DEAUTH:
        case MGT_DISASSOC: {
            s_ctr.deauths++;
            if (mac_zero(&frame[10])) {
                return;
            }
            uint32_t idx = station_find_or_add(&frame[10]);
            if (idx == UINT32_MAX) {
                return;
            }
            memcpy(s_stations[idx].ap, &frame[16], 6);
            s_stations[idx].rssi = rssi;
            s_stations[idx].last_seen_ms = now_ms;
            break;
        }
        default:
            break;
        }
        return;
    }

    if (type == 2) { /* data */
        s_ctr.data++;
        uint32_t store_off = 24; /* QoS bit changes header size, ignore for now */
        (void)store_off;

        /* Map client <-> AP using the DS bits. */
        uint8_t to_ds = FC_TO_DS(frame) != 0;
        uint8_t from_ds = FC_FROM_DS(frame) != 0;
        uint8_t client[6], ap[6];

        if (to_ds && !from_ds) {
            memcpy(client, &frame[10], 6); /* SA */
            memcpy(ap, &frame[16], 6);     /* BSSID */
        } else if (from_ds && !to_ds) {
            memcpy(client, &frame[4], 6);  /* DA */
            memcpy(ap, &frame[16], 6);     /* BSSID */
        } else {
            if (to_ds) {
                memcpy(client, &frame[10], 6);
                memcpy(ap, &frame[16], 6);
            } else {
                return;
            }
        }

        if (mac_zero(client) || mac_zero(ap)) {
            return;
        }

        uint32_t idx = station_find_or_add(client);
        if (idx == UINT32_MAX) {
            return;
        }
        memcpy(s_stations[idx].ap, ap, 6);
        s_stations[idx].rssi = rssi;
        s_stations[idx].last_seen_ms = now_ms;

        /* EAPOL: hunt for the 0x88 0x8E ethertype past the LLC/SNAP. */
        for (uint32_t i = 24; i + 1 < len; i++) {
            if (frame[i] == 0x88 && frame[i + 1] == 0x8E) {
                s_ctr.eapol++;
                break;
            }
        }
        return;
    }

    /* control or unknown: ignore */
}

/* ------------------------------------------------------------------ */
/*  Queries                                                           */
/* ------------------------------------------------------------------ */

uint32_t recon_core_ap_count(void) { return s_ap_count; }
const recon_ap_t *recon_core_ap_get(uint32_t i) {
    return i < s_ap_count ? &s_aps[i] : NULL;
}
uint32_t recon_core_station_count(void) { return s_station_count; }
const recon_station_t *recon_core_station_get(uint32_t i) {
    return i < s_station_count ? &s_stations[i] : NULL;
}
const recon_counters_t *recon_core_counters(void) { return &s_ctr; }

uint32_t recon_core_ap_find(const uint8_t bssid[6]) {
    for (uint32_t i = 0; i < s_ap_count; i++) {
        if (memcmp(s_aps[i].bssid, bssid, 6) == 0) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool recon_core_ap_ssid(const uint8_t bssid[6], char *out, uint32_t out_cap) {
    uint32_t i = recon_core_ap_find(bssid);
    if (i == UINT32_MAX || out_cap == 0) {
        return false;
    }
    snprintf(out, out_cap, "%s", s_aps[i].ssid[0] ? s_aps[i].ssid : "(hidden)");
    return true;
}

uint32_t recon_core_ssids(char out[][RECON_SSID_LEN], uint32_t n) {
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < s_ap_count && cnt < n; i++) {
        if (s_aps[i].ssid[0] == '\0') {
            continue; /* skip hidden */
        }
        memcpy(out[cnt], s_aps[i].ssid, RECON_SSID_LEN);
        cnt++;
    }
    return cnt;
}

uint32_t recon_core_aps(uint8_t out[][6], uint32_t n) {
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < s_ap_count && cnt < n; i++) {
        memcpy(out[cnt], s_aps[i].bssid, 6);
        cnt++;
    }
    return cnt;
}