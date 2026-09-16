#include "unity.h"
#include "recon_core.h"

#include <string.h>

static void put_mac(uint8_t *out, const uint8_t m[6]) {
    memcpy(out, m, 6);
}

static const uint8_t bssid_a[6] = {0xaa, 0xbb, 0x01, 0x22, 0x33, 0x01};
static const uint8_t bssid_b[6] = {0xaa, 0xbb, 0x02, 0x22, 0x33, 0x02};
static const uint8_t client_a[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t client_b[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x66};

/* Build a management frame header. `hdr` must have FC + we fill
   addr1/2/3 and return 24 bytes used. */
static int mgmt_frame(uint8_t *f, uint8_t fc0, uint8_t fc1,
                      const uint8_t addr1[6], const uint8_t addr2[6],
                      const uint8_t addr3[6]) {
    f[0] = fc0;
    f[1] = fc1;
    f[2] = 0;
    f[3] = 0;
    put_mac(&f[4], addr1);
    put_mac(&f[10], addr2);
    put_mac(&f[16], addr3);
    f[22] = 0x10;
    f[23] = 0x00;
    return 24;
}

static void tag(uint8_t *f, int *pos, uint8_t id, const uint8_t *data,
                uint8_t len) {
    f[(*pos)++] = id;
    f[(*pos)++] = len;
    memcpy(&f[*pos], data, len);
    *pos += len;
}

static int beacon_frame(uint8_t *f, const uint8_t bssid[6], uint8_t channel,
                        const char *ssid, int with_rsn) {
    int len = mgmt_frame(f, 0x80, 0x00, (const uint8_t[6]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
                         bssid, bssid);
    /* fixed params */
    f[32] = 0x64;
    f[33] = 0x00;
    f[34] = 0x01;
    f[35] = 0x00;
    int pos = 36;
    tag(f, &pos, 0, (const uint8_t *)ssid, (uint8_t)strlen(ssid));
    static const uint8_t rates[8] = {1, 8, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30};
    tag(f, &pos, 1, rates, 8);
    tag(f, &pos, 3, &channel, 1);
    if (with_rsn) {
        static const uint8_t rsn[2] = {0x01, 0x00};
        tag(f, &pos, 48, rsn, 2);
    }
    return pos;
}

void setUp(void) { recon_core_reset(); }
void tearDown(void) {}

void test_beacon_populates_ap_table(void) {
    uint8_t f[128];
    int len = beacon_frame(f, bssid_a, 6, "TestNet", 1);

    recon_core_parse(f, (uint32_t)len, 6, -45, 1000);

    TEST_ASSERT_EQUAL_UINT32(1, recon_core_ap_count());
    const recon_ap_t *ap = recon_core_ap_get(0);
    TEST_ASSERT_EQUAL_STRING("TestNet", ap->ssid);
    TEST_ASSERT_EQUAL_UINT8(6, ap->channel);
    TEST_ASSERT_EQUAL_INT8(-45, ap->rssi);
    TEST_ASSERT_EQUAL_UINT8(RECON_AUTH_WPA2, ap->authmode);
    TEST_ASSERT_EQUAL_UINT32(recon_core_counters()->beacons, 1);
}

void test_beacon_without_rsn_is_unknown(void) {
    uint8_t f[128];
    int len = beacon_frame(f, bssid_b, 1, "OpenNet", 0);
    recon_core_parse(f, (uint32_t)len, 1, -70, 1500);

    const recon_ap_t *ap = recon_core_ap_get(0);
    TEST_ASSERT_EQUAL_STRING("OpenNet", ap->ssid);
    TEST_ASSERT_EQUAL_UINT8(RECON_AUTH_UNKNOWN, ap->authmode);
}

void test_repeated_beacon_updates_only(void) {
    uint8_t f[128];
    int len = beacon_frame(f, bssid_a, 6, "TestNet", 1);
    recon_core_parse(f, (uint32_t)len, 6, -45, 1000);
    recon_core_parse(f, (uint32_t)len, 6, -50, 2000);

    TEST_ASSERT_EQUAL_UINT32(1, recon_core_ap_count());
    TEST_ASSERT_EQUAL_INT8(-50, recon_core_ap_get(0)->rssi);
    TEST_ASSERT_EQUAL_UINT32(2, recon_core_counters()->beacons);
}

void test_probe_request_tracks_station(void) {
    uint8_t f[32];
    int len = mgmt_frame(f, 0x40, 0x00,
                         (const uint8_t[6]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
                         client_a,
                         (const uint8_t[6]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    recon_core_parse(f, (uint32_t)len, 3, -60, 500);

    TEST_ASSERT_EQUAL_UINT32(1, recon_core_station_count());
    const recon_station_t *st = recon_core_station_get(0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(client_a, st->mac, 6);
    TEST_ASSERT_EQUAL_UINT32(1, recon_core_counters()->probe_reqs);
}

void test_data_frame_maps_client_and_eapol(void) {
    uint8_t f[64];
    /* ToDS data frame */
    f[0] = 0x08;
    f[1] = 0x01;
    f[2] = 0;
    f[3] = 0;
    put_mac(&f[4], bssid_a);  /* addr1 = AP */
    put_mac(&f[10], client_b); /* addr2 = client */
    put_mac(&f[16], bssid_a);  /* addr3 = BSSID */
    f[22] = 0x20;
    f[23] = 0x00;
    /* LLC/SNAP + EAPOL */
    static const uint8_t eapol[12] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00,
                                      0x88, 0x8e, 0x03, 0x00, 0x00, 0x0f};
    memcpy(&f[24], eapol, sizeof(eapol));

    recon_core_parse(f, 24 + (uint32_t)sizeof(eapol), 6, -52, 700);

    const recon_counters_t *ctr = recon_core_counters();
    TEST_ASSERT_EQUAL_UINT32(1, ctr->data);
    TEST_ASSERT_EQUAL_UINT32(1, ctr->eapol);

    TEST_ASSERT_EQUAL_UINT32(1, recon_core_station_count());
    const recon_station_t *st = recon_core_station_get(0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(client_b, st->mac, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid_a, st->ap, 6);
}

void test_deauth_frame_counted(void) {
    uint8_t f[32];
    int len = mgmt_frame(f, 0xC0, 0x00, bssid_a, client_b, bssid_a);
    f[24] = 0x07; /* reason */
    f[25] = 0x00;
    recon_core_parse(f, (uint32_t)(len + 2), 6, -55, 900);

    TEST_ASSERT_EQUAL_UINT32(1, recon_core_counters()->deauths);
    TEST_ASSERT_EQUAL_UINT32(1, recon_core_station_count());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(client_b, recon_core_station_get(0)->mac, 6);
}

void test_queries_and_extractors(void) {
    uint8_t f[128];
    int len = beacon_frame(f, bssid_a, 6, "TestNet", 1);
    recon_core_parse(f, (uint32_t)len, 6, -45, 1000);

    TEST_ASSERT_EQUAL_UINT32(0, recon_core_ap_find(bssid_a));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, recon_core_ap_find(bssid_b));

    char out[RECON_SSID_LEN];
    TEST_ASSERT_TRUE(recon_core_ap_ssid(bssid_a, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("TestNet", out);
    TEST_ASSERT_FALSE(recon_core_ap_ssid(bssid_b, out, sizeof(out)));

    char ssids[2][RECON_SSID_LEN];
    TEST_ASSERT_EQUAL_UINT32(1, recon_core_ssids(ssids, 2));
    TEST_ASSERT_EQUAL_STRING("TestNet", ssids[0]);

    uint8_t bss[2][6];
    TEST_ASSERT_EQUAL_UINT32(1, recon_core_aps(bss, 2));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid_a, bss[0], 6);

    /* hidden AP: empty SSID tag marks hidden but keeps the name */
    uint8_t g[128];
    int glen = mgmt_frame(g, 0x80, 0x00,
                          (const uint8_t[6]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
                          bssid_a, bssid_a);
    g[34] = 0x01;
    int pos = 36;
    tag(g, &pos, 0, (const uint8_t *)"", 0); /* hidden SSID */
    recon_core_parse(g, (uint32_t)pos, 6, -48, 3000);
    TEST_ASSERT_TRUE(recon_core_ap_get(0)->hidden);
}

void test_junk_and_short_frames_ignored(void) {
    /* too short */
    uint8_t short_frame[10] = {0};
    recon_core_parse(short_frame, 10, 1, -60, 1);

    /* NULL */
    recon_core_parse(NULL, 10, 1, -60, 1);

    /* control frame (type 1, e.g. CTS) is ignored */
    uint8_t f[32];
    int len = mgmt_frame(f, 0xC4, 0x00,
                         (const uint8_t[6]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
                         bssid_a,
                         (const uint8_t[6]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    recon_core_parse(f, (uint32_t)len, 1, -60, 2);

    TEST_ASSERT_EQUAL_UINT32(0, recon_core_ap_count());
    TEST_ASSERT_EQUAL_UINT32(0, recon_core_station_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_beacon_populates_ap_table);
    RUN_TEST(test_beacon_without_rsn_is_unknown);
    RUN_TEST(test_repeated_beacon_updates_only);
    RUN_TEST(test_probe_request_tracks_station);
    RUN_TEST(test_data_frame_maps_client_and_eapol);
    RUN_TEST(test_deauth_frame_counted);
    RUN_TEST(test_queries_and_extractors);
    RUN_TEST(test_junk_and_short_frames_ignored);
    return UNITY_END();
}