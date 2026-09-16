#include "harvest.h"

#include <string.h>

/* EAPOL-Key Key Information bits (bits are LSB-first within the 16-bit value,
   as stored little-endian on the wire). */
#define HARVEST_KEY_PAIR 0x0008u /* Key Type: pairwise */
#define HARVEST_KEY_ACK  0x0080u /* response required (M1, M3) */
#define HARVEST_KEY_MIC  0x0100u /* MIC valid (M2, M4) */
#define HARVEST_KEY_SEC  0x0200u /* keys installed (M3, M4) */

static harvest_pair_t s_pairs[HARVEST_PAIR_MAX];
static uint32_t s_pair_count = 0;
static harvest_frame_t s_log[HARVEST_LOG_MAX];
static uint32_t s_log_count = 0;
static uint32_t s_log_head = 0; /* FIFO: s_log_count frames from head */
static uint32_t s_clock = 0;    /* monotonic feed-order clock for eviction */

void harvest_reset(void) {
    memset(s_pairs, 0, sizeof(s_pairs));
    memset(s_log, 0, sizeof(s_log));
    s_pair_count = 0;
    s_log_count = 0;
    s_log_head = 0;
    s_clock = 0;
}

/* ------------------------------------------------------------------ */
/*  Pair bookkeeping                                                   */
/* ------------------------------------------------------------------ */

static bool mac_zero(const uint8_t m[6]) {
    return m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 0 &&
           m[5] == 0;
}

/* Find the pair slot for (ap, sta), or create/evict one. Returns NULL if the
   MACs are meaningless or the table is full of live pairs we refuse to drop. */
static harvest_pair_t *pair_find_or_add(const uint8_t ap[6],
                                        const uint8_t sta[6]) {
    for (uint32_t i = 0; i < s_pair_count; i++) {
        if (memcmp(s_pairs[i].ap, ap, 6) == 0 &&
            memcmp(s_pairs[i].sta, sta, 6) == 0) {
            return &s_pairs[i];
        }
    }
    if (s_pair_count < HARVEST_PAIR_MAX) {
        harvest_pair_t *p = &s_pairs[s_pair_count++];
        memcpy(p->ap, ap, 6);
        memcpy(p->sta, sta, 6);
        p->msgs = 0;
        p->has_pmkid = false;
        return p;
    }
    /* Full: evict the least-recently-active, prefer stale incomplete pairs. */
    uint32_t victim = 0;
    for (uint32_t i = 1; i < s_pair_count; i++) {
        if (s_pairs[i].first_ms < s_pairs[victim].first_ms) {
            victim = i;
        }
    }
    memset(&s_pairs[victim], 0, sizeof(s_pairs[victim]));
    memcpy(s_pairs[victim].ap, ap, 6);
    memcpy(s_pairs[victim].sta, sta, 6);
    return &s_pairs[victim];
}

bool harvest_pair_ready(const harvest_pair_t *p) {
    if (!p) {
        return false;
    }
    bool has_mic = (p->msgs & (HARVEST_MSG_M2 | HARVEST_MSG_M4)) != 0;
    bool has_anonce = (p->msgs & (HARVEST_MSG_M1 | HARVEST_MSG_M3)) != 0;
    return has_mic && has_anonce;
}

/* ------------------------------------------------------------------ */
/*  Frame log (FIFO)                                                   */
/* ------------------------------------------------------------------ */

static void log_push(const uint8_t *frame, uint32_t len, uint8_t channel,
                     uint8_t msg) {
    harvest_frame_t *f = &s_log[s_log_head];
    f->msg = msg;
    f->channel = channel;
    uint32_t keep = len > HARVEST_FRAME_MAX ? HARVEST_FRAME_MAX : len;
    f->len = (uint16_t)keep;
    memcpy(f->data, frame, keep);
    if (s_log_count < HARVEST_LOG_MAX) {
        s_log_count++;
    }
    s_log_head = (s_log_head + 1) % HARVEST_LOG_MAX;
}

/* ------------------------------------------------------------------ */
/*  Feed path                                                           */
/* ------------------------------------------------------------------ */

/* PMKID KDE: vendor-specific IE `dd 14 00 0f ac 04` then 16 bytes of PMKID.
   Shipped by the AP inside M1's key data when PMKSA caching is enabled; the
   16 bytes hash as PMKID=HMAC-SHA1-128(PMK, "PMK Name" || aa || spa) and are
   enough to brute-force the PSK with zero client involvement. */
static void pair_extract_pmkid(harvest_pair_t *p, const uint8_t *kdata,
                               uint32_t klen) {
    static const uint8_t kde[6] = {0xdd, 0x14, 0x00, 0x0f, 0xac, 0x04};
    for (uint32_t i = 0; i + 6 + 16 <= klen; i++) {
        if (memcmp(&kdata[i], kde, 6) == 0) {
            memcpy(p->pmkid, &kdata[i + 6], 16);
            p->has_pmkid = true;
            return;
        }
    }
}

/* Walk the frame from the start of the EAPOL key descriptor and pull any
   PMKID KDE out of the trailing key-data payload. */
static void pmkid_from_frame(harvest_pair_t *p, const uint8_t *frame,
                             uint32_t kd, uint32_t len) {
    uint8_t version = frame[kd];
    uint32_t fixed = version >= 2 ? 97u : 96u; /* key-data length field */
    if (kd + fixed > len) {
        return;
    }
    pair_extract_pmkid(p, &frame[kd + fixed], len - (kd + fixed));
}

void harvest_feed(const uint8_t *frame, uint32_t len, uint8_t channel) {
    if (!frame || len < 24) {
        return;
    }
    if (((frame[0] >> 2) & 0x03u) != 2u) {
        return; /* management / control */
    }

    uint8_t to_ds = frame[1] & 0x01u;
    uint8_t from_ds = (frame[1] >> 1) & 0x01u;
    if (to_ds == from_ds) {
        return; /* WDS/adhoc: no clean AP<->STA pair */
    }

    uint8_t sub = (frame[0] >> 4) & 0x0Fu;
    uint32_t hdr = 24;
    if (sub & 0x08u) hdr += 2;      /* QoS control */
    if (to_ds && from_ds) hdr += 6; /* addr4 (never hit: to_ds==from_ds above) */

    /* LLC/SNAP + 802.1X + key_info must fit */
    if (hdr + 8 + 4 + 2 > len) {
        return;
    }

    /* LLC/SNAP 0xaa aa 03 00 00 00 then EtherType 88 8e (EAPoL) */
    if (frame[hdr] != 0xaa || frame[hdr + 1] != 0xaa ||
        frame[hdr + 2] != 0x03 || frame[hdr + 3] != 0x00 ||
        frame[hdr + 4] != 0x00 || frame[hdr + 5] != 0x00 ||
        frame[hdr + 6] != 0x88 || frame[hdr + 7] != 0x8e) {
        return;
    }

    uint32_t eo = hdr + 8; /* EAPOL header start */
    if (frame[eo + 1] != 3u) {
        return; /* not an EAPOL-Key */
    }

    /* Key descriptor: 2-byte version (LSB first), 1-byte descriptor type
       (254 = RSN), then the 2-byte Key Information. */
    uint32_t kd = eo + 4;
    if (kd + 5 > len) {
        return;
    }

    uint16_t kinfo = (uint16_t)(frame[kd + 3] | ((uint16_t)frame[kd + 4] << 8));
    if (!(kinfo & HARVEST_KEY_PAIR)) {
        return; /* group-key handshake: useless for cracking */
    }

    uint8_t msg;
    bool ack = (kinfo & HARVEST_KEY_ACK) != 0;
    bool mic = (kinfo & HARVEST_KEY_MIC) != 0;
    bool sec = (kinfo & HARVEST_KEY_SEC) != 0;
    if (!mic && ack) {
        msg = 1;
    } else if (mic && !ack && !sec) {
        msg = 2;
    } else if (mic && ack && sec) {
        msg = 3;
    } else if (mic && !ack && sec) {
        msg = 4;
    } else {
        return; /* stray/unclassified */
    }

    /* Pair identity from the header: the BSSID lives in addr3 for both
       ToDS (client->AP) and FromDS (AP->client) data frames. */
    const uint8_t *bssid = &frame[16];
    const uint8_t *client = to_ds ? &frame[10] /* SA */ : &frame[4] /* DA */;
    if (mac_zero(bssid) || mac_zero(client)) {
        return;
    }

    harvest_pair_t *p = pair_find_or_add(bssid, client);
    if (!p) {
        return;
    }

    uint32_t bit = 1u << (msg - 1);
    if (p->msgs & bit) {
        return; /* duplicate of what we already have: skip */
    }
    p->msgs |= bit;
    if (p->first_ms == 0) {
        p->first_ms = ++s_clock;
    }
    if (!p->has_pmkid) {
        pmkid_from_frame(p, frame, kd, len);
    }

    log_push(frame, len, channel, msg);
}

/* ------------------------------------------------------------------ */
/*  Queries                                                            */
/* ------------------------------------------------------------------ */

uint32_t harvest_pair_count(void) { return s_pair_count; }

bool harvest_pair_get(uint32_t i, harvest_pair_t *out) {
    if (i >= s_pair_count || !out) {
        return false;
    }
    *out = s_pairs[i];
    return true;
}

uint32_t harvest_ready_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < s_pair_count; i++) {
        if (harvest_pair_ready(&s_pairs[i])) {
            n++;
        }
    }
    return n;
}

uint32_t harvest_frame_count(void) { return s_log_count; }

/* ------------------------------------------------------------------ */
/*  pcap export                                                        */
/* ------------------------------------------------------------------ */

/* Ordered (oldest -> newest) index into the FIFO. */
static uint32_t log_index(uint32_t i) {
    uint32_t oldest = (s_log_head + HARVEST_LOG_MAX - s_log_count) % HARVEST_LOG_MAX;
    return (oldest + i) % HARVEST_LOG_MAX;
}

size_t harvest_pcap_size(void) {
    size_t n = 24; /* libpcap global header */
    for (uint32_t i = 0; i < s_log_count; i++) {
        n += 16 + s_log[log_index(i)].len;
    }
    return n;
}

static uint32_t be32(uint32_t v) {
    return ((v & 0xff000000u) >> 24) | ((v & 0x00ff0000u) >> 8) |
           ((v & 0x0000ff00u) << 8) | ((v & 0x000000ffu) << 24);
}

size_t harvest_build_pcap(uint8_t *buf, size_t cap) {
    if (!buf || cap < harvest_pcap_size()) {
        return 0;
    }
    size_t w = 0;

    /* legacy little-endian pcap magic, v2.4, snaplen 64k, linktype 105 */
    uint8_t gh[24];
    gh[0] = 0xd4; gh[1] = 0xc3; gh[2] = 0xb2; gh[3] = 0xa1; /* 0xa1b2c3d4 LE */
    memset(&gh[4], 0, 16);
    gh[20] = 0x69; gh[21] = 0x00; /* 105 */
    gh[22] = 0x00; gh[23] = 0x00;
    memcpy(&buf[w], gh, 24);
    w += 24;

    for (uint32_t i = 0; i < s_log_count; i++) {
        const harvest_frame_t *f = &s_log[log_index(i)];
        uint32_t ts = be32(1u); /* seconds since epoch; arbitrary */
        uint32_t tu = be32(0u);
        uint32_t incl = be32(f->len);
        uint32_t orig = be32(f->len);
        memcpy(&buf[w], &ts, 4);
        memcpy(&buf[w + 4], &tu, 4);
        memcpy(&buf[w + 8], &incl, 4);
        memcpy(&buf[w + 12], &orig, 4);
        w += 16;
        memcpy(&buf[w], f->data, f->len);
        w += f->len;
    }
    return w;
}