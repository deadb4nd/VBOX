#include "unity.h"
#include <settings.h>

#include <string.h>

void test_defaults_are_sane(void) {
    velo_settings_t s;
    settings_init_default(&s);

    TEST_ASSERT_EQUAL_INT(SETTING_ACTION_DEAUTH, s.default_action);
    TEST_ASSERT_EQUAL_UINT8(1, s.fakeap_channel);
    TEST_ASSERT_EQUAL_UINT8(4, s.fakeap_max_connections);
    TEST_ASSERT_EQUAL_UINT16(100, s.fakeap_beacon_interval);
    TEST_ASSERT_TRUE(s.ble_spam_enabled);
    TEST_ASSERT_TRUE(s.ssid_count > 0);
    TEST_ASSERT_TRUE(s.ssid_count <= SETTINGS_MAX_SSIDS);
}

void test_out_of_range_values_rejected(void) {
    velo_settings_t s;
    settings_init_default(&s);

    TEST_ASSERT_FALSE(settings_set_default_action(&s, SETTING_ACTION_COUNT));
    TEST_ASSERT_FALSE(settings_set_fakeap_channel(&s, 0));
    TEST_ASSERT_FALSE(settings_set_fakeap_channel(&s, 15));
    TEST_ASSERT_FALSE(settings_set_fakeap_max_connections(&s, 0));
    TEST_ASSERT_FALSE(settings_set_fakeap_max_connections(&s, 9));
    TEST_ASSERT_FALSE(
        settings_set_fakeap_beacon_interval(&s, 10001));

    TEST_ASSERT_TRUE(settings_set_fakeap_channel(&s, 11));
    TEST_ASSERT_EQUAL_UINT8(11, s.fakeap_channel);
}

void test_ssid_add_and_bounds(void) {
    velo_settings_t s;
    settings_init_default(&s);

    uint32_t before = s.ssid_count;
    TEST_ASSERT_TRUE(settings_add_ssid(&s, "Test Net"));
    TEST_ASSERT_EQUAL_UINT32(before + 1, s.ssid_count);

    char out[SETTINGS_SSID_MAX_LEN];
    TEST_ASSERT_TRUE(settings_get_ssid(&s, before, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Test Net", out);

    /* empty string rejected */
    TEST_ASSERT_FALSE(settings_add_ssid(&s, ""));

    /* list can't exceed max */
    velo_settings_t full;
    settings_init_default(&full);
    for (uint32_t i = full.ssid_count; i < SETTINGS_MAX_SSIDS; i++) {
        settings_add_ssid(&full, "x");
    }
    TEST_ASSERT_EQUAL_UINT32(SETTINGS_MAX_SSIDS, full.ssid_count);
    TEST_ASSERT_FALSE(settings_add_ssid(&full, "y"));
    TEST_ASSERT_FALSE(settings_get_ssid(&full, SETTINGS_MAX_SSIDS, out,
                                        sizeof(out)));
}

void test_ssids_text_roundtrip(void) {
    velo_settings_t s;
    settings_init_default(&s);

    const char *text = "Alpha\nBeta Net\n\nGamma\r\n";
    TEST_ASSERT_TRUE(settings_set_ssids_text(&s, text));
    TEST_ASSERT_EQUAL_UINT32(3, s.ssid_count);

    char buf[256];
    size_t used = settings_ssids_to_text(&s, buf, sizeof(buf));
    TEST_ASSERT_TRUE(used > 0);
    TEST_ASSERT_EQUAL_STRING("Alpha\nBeta Net\nGamma\n", buf);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_sane);
    RUN_TEST(test_out_of_range_values_rejected);
    RUN_TEST(test_ssid_add_and_bounds);
    RUN_TEST(test_ssids_text_roundtrip);
    return UNITY_END();
}