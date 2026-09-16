#include "unity.h"
#include "harvest.h"

#include <string.h>

static const uint8_t bssid[6] = {0xaa, 0xbb, 0x01, 0x22, 0x33, 0x01};
static const uint8_t client[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t client2[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x66};

static void put64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t)(v & 0xff);
        v >>= 8;
    }
}

/* Build a QoS/plain data frame carrying one EAPOL-Key message.
   `from_ap`: true -> FromDS (AP->client, M1/M3), false -> ToDS (M2/M4).
   key_info carries the wire-order little-endian Key Information. */
static int eapol_frame(uint8_t *f, bool from_ap, uint16_t key_info,
                       uint64_t replay) {
    memset(f, 0, 128);
    /* 802.11 data header */
    f[0] = 0x08;
    f[1] = from_ap ? 0x02 /* FromDS */ : 0x01 /* ToDS */;
    if (from_ap) {
        memcpy(&f[4], client, 6); /* addr1 = DA */
        memcpy(&f[10], bssid, 6); /* addr2 = SA */
    } else {
        memcpy(&f[4], bssid, 6);  /* addr1 = RA */
        memcpy(&f[10], client, 6); /* addr2 = SA */
    }
    memcpy(&f[16], bssid, 6); /* addr3 = BSSID */

    /* LLC/SNAP + 0x88 0x8e */
    static const uint8_t llc[8] = {0xaa, 0xaa, 0x03, 0x00,
                                   0x00, 0x00, 0x88, 0x8e};
    memcpy(&f[24], llc, 8);

    int p = 32; /* EAPOL header */
    f[p] = 3;   /* EAPOL version */
    f[p + 1] = 3;
    p += 4; /* length patched below */

    /* key descriptor */
    f[p] = (uint8_t)(key_info & 0xff);
    f[p + 1] = (uint8_t)(key_info >> 8);
    p += 2;
    f[p] = 16; /* key length */
    f[p + 1] = 0;
    p += 2;
    put64(&f[p], replay);
    p += 8;
    for (int i = 0; i < 32; i++) f[p + i] = (uint8_t)(0x40 + i); /* nonce */
    p += 32;
    p += 16; /* iv */
    p += 8;  /* rsc */
    p += 8;  /* id */
    p += 16; /* mic */
    uint16_t kdlen = 0;
    f[p] = (uint8_t)(kdlen & 0xff);
    f[p + 1] = (uint8_t)(kdlen >> 8);
    p += 2;

    uint16_t eapol_len = (uint16_t)(p - 32);
    f[34] = (uint8_t)(eapol_len >> 8); /* EAPOL length */
    f[35] = (uint8_t)(eapol_len & 0xff);
    return p;
}

#define KI_VER2_PAIR (0x0002u | 0x0008u)
#define M1 (KI_VER2_PAIR | 0x0080u) /* ack */
#define M2 (KI_VER2_PAIR | 0x0100u) /* mic */
#define M3 (KI_VER2_PAIR | 0x0080u | 0x0100u | 0x0200u)
#define M4 (KI_VER2_PAIR | 0x0100u | 0x0200u)

void setUp(void) { harvest_reset(); }
void tearDown(void) {}

void test_full_handshake_captured(void) {
    uint8_t f[128];
    int len;

    len = eapol_frame(f, true, M1, 1);
    harvest_feed(f, (uint32_t)len, 6);
    len = eapol_frame(f, false, M2, 2);
    harvest_feed(f, (uint32_t)len, 6);
    len = eapol_frame(f, true, M3, 3);
    harvest_feed(f, (uint32_t)len, 6);
    len = eapol_frame(f, false, M4, 4);
    harvest_feed(f, (uint32_t)len, 6);

    TEST_ASSERT_EQUAL_UINT32(1, harvest_pair_count());
    TEST_ASSERT_EQUAL_UINT32(1, harvest_ready_count());
    TEST_ASSERT_EQUAL_UINT32(4, harvest_frame_count());

    harvest_pair_t p;
    TEST_ASSERT_TRUE(harvest_pair_get(0, &p));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, p.ap, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(client, p.sta, 6);
    uint8_t want = HARVEST_MSG_M1 | HARVEST_MSG_M2 | HARVEST_MSG_M3 |
                   HARVEST_MSG_M4;
    TEST_ASSERT_EQUAL_UINT8(want, p.msgs);
}

void test_second_client_tracked_separately(void) {
    uint8_t f[128];
    int len = eapol_frame(f, true, M1, 1);
    harvest_feed(f, (uint32_t)len, 6);
    len = eapol_frame(f, false, M2, 2);
    harvest_feed(f, (uint32_t)len, 6);

    /* second client, M1 only */
    uint8_t g[128];
    int glen = eapol_frame(g, true, M1, 1);
    /* retarget the frame to client2 */
    memcpy(&g[4], client2, 6);
    memcpy(&g[10], bssid, 6);
    harvest_feed(g, (uint32_t)glen, 6);

    TEST_ASSERT_EQUAL_UINT32(2, harvest_pair_count());
    TEST_ASSERT_EQUAL_UINT32(1, harvest_ready_count());
}

void test_duplicate_frames_ignored(void) {
    uint8_t f[128];
    int len = eapol_frame(f, true, M1, 7);
    harvest_feed(f, (uint32_t)len, 6);
    harvest_feed(f, (uint32_t)len, 6); /* same replay, same pair, same msg */

    TEST_ASSERT_EQUAL_UINT32(1, harvest_frame_count());
    TEST_ASSERT_EQUAL_UINT32(0, harvest_ready_count());
}

void test_group_key_ignored(void) {
    uint8_t f[128];
    int len = eapol_frame(f, true, 0x0080u | 0x0002u, 1); /* no pairwise bit */
    harvest_feed(f, (uint32_t)len, 6);

    TEST_ASSERT_EQUAL_UINT32(0, harvest_pair_count());
    TEST_ASSERT_EQUAL_UINT32(0, harvest_frame_count());
}

void test_non_eapol_and_junk_ignored(void) {
    uint8_t f[128];
    int len = eapol_frame(f, true, M1, 1);
    f[30] = 0x09; /* corrupt EtherType: not 0x88 0x8e */
    harvest_feed(f, (uint32_t)len, 6);

    uint8_t mgmt[32] = {0};
    mgmt[0] = 0x80; /* beacon */
    harvest_feed(mgmt, 32, 1);

    harvest_feed(NULL, 10, 1);

    uint8_t shortf[20] = {0};
    harvest_feed(shortf, 20, 1);

    TEST_ASSERT_EQUAL_UINT32(0, harvest_pair_count());
    TEST_ASSERT_EQUAL_UINT32(0, harvest_frame_count());
}

void test_m2_alone_marks_client_then_m1_completes(void) {
    uint8_t f[128];
    int len = eapol_frame(f, false, M2, 2);
    harvest_feed(f, (uint32_t)len, 1);
    TEST_ASSERT_EQUAL_UINT32(1, harvest_pair_count());
    TEST_ASSERT_EQUAL_UINT32(0, harvest_ready_count()); /* mid-handshake */

    len = eapol_frame(f, true, M1, 1);
    harvest_feed(f, (uint32_t)len, 1);
    TEST_ASSERT_EQUAL_UINT32(1, harvest_ready_count());
}

void test_pcap_export(void) {
    uint8_t f[128];
    int len = eapol_frame(f, true, M1, 1);
    harvest_feed(f, (uint32_t)len, 6);
    len = eapol_frame(f, false, M2, 2);
    harvest_feed(f, (uint32_t)len, 6);

    size_t need = harvest_pcap_size();
    /* 802.11 hdr 24 + LLC 8 + EAPOL hdr 4 + descriptor 94 = 130 B/frame */
    TEST_ASSERT_EQUAL_UINT32(24 + 2 * (16 + 130), need);

    uint8_t buf[2048];
    size_t wrote = harvest_build_pcap(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(need, wrote);

    /* little-endian magic 0xa1b2c3d4 */
    TEST_ASSERT_EQUAL_UINT8(0xd4, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xc3, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0xb2, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xa1, buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0x69, buf[20]); /* linktype 105 */
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[21]);

    /* first record header at 24, first frame after it at 24+16 */
    TEST_ASSERT_EQUAL_UINT8(0x08, buf[40]); /* 802.11 frame start */
    TEST_ASSERT_EQUAL_UINT8(0x02, buf[41]); /* FromDS (M1) */

    /* buffer too small -> nothing written */
    buf[0] = 0x00;
    TEST_ASSERT_EQUAL_UINT32(0, harvest_build_pcap(buf, 10));
}

void test_reset_clears_everything(void) {
    uint8_t f[128];
    int len = eapol_frame(f, true, M1, 1);
    harvest_feed(f, (uint32_t)len, 6);

    harvest_reset();
    TEST_ASSERT_EQUAL_UINT32(0, harvest_pair_count());
    TEST_ASSERT_EQUAL_UINT32(0, harvest_ready_count());
    TEST_ASSERT_EQUAL_UINT32(0, harvest_frame_count());
    TEST_ASSERT_EQUAL_UINT32(24, harvest_pcap_size()); /* header only */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_full_handshake_captured);
    RUN_TEST(test_second_client_tracked_separately);
    RUN_TEST(test_duplicate_frames_ignored);
    RUN_TEST(test_group_key_ignored);
    RUN_TEST(test_non_eapol_and_junk_ignored);
    RUN_TEST(test_m2_alone_marks_client_then_m1_completes);
    RUN_TEST(test_pcap_export);
    RUN_TEST(test_reset_clears_everything);
    return UNITY_END();
}