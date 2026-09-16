#pragma once

/* WPA/WPA2 4-way handshake harvester. Pure C on purpose so the logic is
   host-testable. The wifi driver (recon.c) feeds raw 802.11 data frames in;
   harvest picks out pairwise EAPOL-Key messages, tracks each AP/client pair,
   detects when a usable handshake (ANonce source + a MIC frame) has been
   seen, and keeps a compact frame log that can be serialized to a
   legacy-little-endian libpcap (LINKTYPE_IEEE802_11 = 105) for aircrack-ng /
   hcxpcapngtool.

   All access must be serialized by the caller (the driver holds a mutex
   around feed + snapshots). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HARVEST_PAIR_MAX 8      /* distinct AP/client pairs tracked */
#define HARVEST_LOG_MAX 48      /* frames kept for the .pcap */
#define HARVEST_FRAME_MAX 240   /* max bytes of an 802.11 frame we keep */

#define HARVEST_MSG_M1 (1u << 0)
#define HARVEST_MSG_M2 (1u << 1)
#define HARVEST_MSG_M3 (1u << 2)
#define HARVEST_MSG_M4 (1u << 3)

typedef struct {
    uint8_t ap[6];
    uint8_t sta[6];
    uint8_t msgs;       /* HARVEST_MSG_* bitset */
    uint32_t first_ms;  /* when the pair was first seen */
    bool has_pmkid;     /* PMKID present in a seen M1 */
    uint8_t pmkid[16];
} harvest_pair_t;

typedef struct {
    uint8_t  msg;        /* 1..4 for human/UI reference */
    uint8_t  channel;
    uint16_t len;        /* <= HARVEST_FRAME_MAX */
    uint8_t  data[HARVEST_FRAME_MAX];
} harvest_frame_t;

void harvest_reset(void);

/* Feed one raw 802.11 frame (starting at the MAC header). Cheap no-op for
   anything that is not a pairwise EAPOL-Key data frame. */
void harvest_feed(const uint8_t *frame, uint32_t len, uint8_t channel);

/* Snapshot queries. */
uint32_t harvest_pair_count(void);
bool harvest_pair_get(uint32_t i, harvest_pair_t *out);
uint32_t harvest_ready_count(void); /* pairs with a usable handshake */
uint32_t harvest_frame_count(void); /* frames queued for the pcap */

/* A pair carries a usable handshake when we hold one side's nonce (M1 or M3)
   and a MIC frame (M2 or M4). That is enough for offline PMK derivation. */
bool harvest_pair_ready(const harvest_pair_t *p);

/* True when we saw an M1 whose key data carried a PMKID KDE. A PMKID from
   the AP is independent of any client, so with an active probe this is a
   clientless path to the PSK hashset. */
static inline bool harvest_pair_has_pmkid(const harvest_pair_t *p) {
    return p != NULL && p->has_pmkid;
}

/* Build the whole capture as a pcap into `buf`. Returns bytes written, or 0
   when nothing captured / buffer too small. */
size_t harvest_build_pcap(uint8_t *buf, size_t cap);
size_t harvest_pcap_size(void);