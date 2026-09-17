#include "unity.h"
#include "script.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_parse_basic_steps(void) {
    const char *text = "recon 10000\nwait 2000\n";
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(2, script_parse(text, &s));
    TEST_ASSERT_EQUAL(SCRIPT_CMD_RECON, s.steps[0].cmd);
    TEST_ASSERT_EQUAL_UINT32(10000, s.steps[0].duration_ms);
    TEST_ASSERT_EQUAL(SCRIPT_CMD_WAIT, s.steps[1].cmd);
    TEST_ASSERT_EQUAL_UINT32(2000, s.steps[1].duration_ms);
}

void test_comments_and_blank_lines_skipped(void) {
    const char *text = "# header\n\n   \nrecon 3000\n# tail\n";
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(1, script_parse(text, &s));
    TEST_ASSERT_EQUAL(SCRIPT_CMD_RECON, s.steps[0].cmd);
}

void test_default_duration_applied(void) {
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(1, script_parse("blescan\n", &s));
    TEST_ASSERT_EQUAL_UINT32(SCRIPT_DEFAULT_MS, s.steps[0].duration_ms);
}

void test_duration_forms(void) {
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(1, script_parse("probe ms=1234\n", &s));
    TEST_ASSERT_EQUAL_UINT32(1234, s.steps[0].duration_ms);

    TEST_ASSERT_EQUAL_UINT32(1, script_parse("probe 7777\n", &s));
    TEST_ASSERT_EQUAL_UINT32(7777, s.steps[0].duration_ms);
}

void test_target_parsing_forms(void) {
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(
        1, script_parse("deauth ap=aa:bb:cc:dd:ee:01 client=00:11:22:33:44:55 5000\n",
                        &s));
    TEST_ASSERT_TRUE(s.steps[0].has_ap);
    TEST_ASSERT_TRUE(s.steps[0].has_client);
    const uint8_t ap[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01};
    const uint8_t cl[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ap, s.steps[0].ap, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cl, s.steps[0].client, 6);

    /* bare MAC order: first = AP, second = client */
    TEST_ASSERT_EQUAL_UINT32(
        2, script_parse("deauth aabbccddee01 aabbccddee02 4000\nwait 100\n", &s));
    TEST_ASSERT_TRUE(s.steps[0].has_ap);
    TEST_ASSERT_TRUE(s.steps[0].has_client);
    TEST_ASSERT_EQUAL_HEX8(0x01, s.steps[0].ap[5]);
    TEST_ASSERT_EQUAL_HEX8(0x02, s.steps[0].client[5]);
}

void test_broadcast_deauth_has_no_target(void) {
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(1, script_parse("deauth 3000\n", &s));
    TEST_ASSERT_FALSE(s.steps[0].has_ap);
    TEST_ASSERT_FALSE(s.steps[0].has_client);
}

void test_unknown_lines_are_skipped_boundaries(void) {
    const char *text = "bogus 1000\nrecon 5000\nnonsense\nwait 600\n";
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(2, script_parse(text, &s));
    TEST_ASSERT_EQUAL(SCRIPT_CMD_RECON, s.steps[0].cmd);
    TEST_ASSERT_EQUAL(SCRIPT_CMD_WAIT, s.steps[1].cmd);
}

void test_durations_clamped(void) {
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(1, script_parse("recon 10\n", &s));
    TEST_ASSERT_EQUAL_UINT32(SCRIPT_MIN_MS, s.steps[0].duration_ms);

    TEST_ASSERT_EQUAL_UINT32(1, script_parse("recon 99999999\n", &s));
    TEST_ASSERT_EQUAL_UINT32(SCRIPT_MAX_MS, s.steps[0].duration_ms);
}

void test_step_cap_enforced(void) {
    char text[512];
    size_t w = 0;
    for (int i = 0; i < 20; i++) {
        w += (size_t)snprintf(text + w, sizeof(text) - w, "wait 500\n");
    }
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(SCRIPT_MAX_STEPS, script_parse(text, &s));
}

void test_mac_variants(void) {
    uint8_t out[6];
    TEST_ASSERT_TRUE(script_parse_mac("aabbccddeeff", out));
    TEST_ASSERT_EQUAL_HEX8(0xaa, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xff, out[5]);
    TEST_ASSERT_TRUE(script_parse_mac("AA:BB:CC:DD:EE:FF", out));
    TEST_ASSERT_EQUAL_HEX8(0xaa, out[0]);
    TEST_ASSERT_TRUE(script_parse_mac("aa-bb-cc-dd-ee-ff", out));
    TEST_ASSERT_FALSE(script_parse_mac("aabbccddee", out));   /* too short */
    TEST_ASSERT_FALSE(script_parse_mac("zzbbccddeeff", out)); /* bad hex */
    TEST_ASSERT_FALSE(script_parse_mac(NULL, out));
}

void test_roundtrip_to_text(void) {
    script_t s;
    script_parse("deauth aabbccddee01 aabbccddee02 4000\nblescan 2500\n", &s);
    char buf[256];
    size_t n = script_to_text(&s, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "deauth"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "aa:bb:cc:dd:ee:01"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "4000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "blescan"));
}

void test_total_ms(void) {
    script_t s;
    script_parse("recon 10000\nwait 2000\nprobe 3000\n", &s);
    TEST_ASSERT_EQUAL_UINT32(15000, script_total_ms(&s));
    TEST_ASSERT_EQUAL_UINT32(0, script_total_ms(NULL));
}

void test_empty_input_yields_zero(void) {
    script_t s;
    TEST_ASSERT_EQUAL_UINT32(0, script_parse("", &s));
    TEST_ASSERT_EQUAL_UINT32(0, script_parse("# only a comment\n", &s));
    TEST_ASSERT_EQUAL_UINT32(0, script_parse(NULL, &s));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_basic_steps);
    RUN_TEST(test_comments_and_blank_lines_skipped);
    RUN_TEST(test_default_duration_applied);
    RUN_TEST(test_duration_forms);
    RUN_TEST(test_target_parsing_forms);
    RUN_TEST(test_broadcast_deauth_has_no_target);
    RUN_TEST(test_unknown_lines_are_skipped_boundaries);
    RUN_TEST(test_durations_clamped);
    RUN_TEST(test_step_cap_enforced);
    RUN_TEST(test_mac_variants);
    RUN_TEST(test_roundtrip_to_text);
    RUN_TEST(test_total_ms);
    RUN_TEST(test_empty_input_yields_zero);
    return UNITY_END();
}
