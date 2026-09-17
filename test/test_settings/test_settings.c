#include "unity.h"
#include <settings.h>

#include <string.h>

void test_defaults_are_sane(void) {
    velo_settings_t s;
    settings_init_default(&s);

    TEST_ASSERT_EQUAL_INT(SETTING_ACTION_DEAUTH, s.default_action);
    TEST_ASSERT_EQUAL_UINT16(1500, s.idle_timeout_ms);
    TEST_ASSERT_EQUAL_UINT16(2000, s.warning_duration_ms);
    TEST_ASSERT_EQUAL_UINT8(1, s.fakeap_channel);
    TEST_ASSERT_EQUAL_UINT8(4, s.fakeap_max_connections);
    TEST_ASSERT_EQUAL_UINT16(100, s.fakeap_beacon_interval);
    TEST_ASSERT_TRUE(s.ble_spam_enabled);
    TEST_ASSERT_EQUAL_STRING("VeloBox", s.ap_ssid);
    TEST_ASSERT_TRUE(s.ssid_count > 0);
    TEST_ASSERT_TRUE(s.ssid_count <= SETTINGS_MAX_SSIDS);
    TEST_ASSERT_EQUAL_UINT32(0, s.sleep_timeout_ms);
}

void test_sleep_timeout_bounds(void) {
    velo_settings_t s;
    settings_init_default(&s);

    TEST_ASSERT_FALSE(settings_set_sleep_timeout_ms(&s, 1000));      /* too small */
    TEST_ASSERT_FALSE(settings_set_sleep_timeout_ms(&s, 3600001));   /* too big */
    TEST_ASSERT_TRUE(settings_set_sleep_timeout_ms(&s, 0));          /* off */
    TEST_ASSERT_EQUAL_UINT32(0, s.sleep_timeout_ms);
    TEST_ASSERT_TRUE(settings_set_sleep_timeout_ms(&s, 300000));     /* 5 min */
    TEST_ASSERT_EQUAL_UINT32(300000, s.sleep_timeout_ms);
    TEST_ASSERT_FALSE(settings_set_sleep_timeout_ms(NULL, 300000));
}

void test_out_of_range_values_rejected(void) {
    velo_settings_t s;
    settings_init_default(&s);

    TEST_ASSERT_FALSE(settings_set_default_action(&s, SETTING_ACTION_COUNT));
    TEST_ASSERT_FALSE(settings_set_idle_timeout_ms(&s, 499));
    TEST_ASSERT_FALSE(settings_set_idle_timeout_ms(&s, 10001));
    TEST_ASSERT_FALSE(settings_set_warning_duration_ms(&s, 499));
    TEST_ASSERT_FALSE(settings_set_warning_duration_ms(&s, 10001));
    TEST_ASSERT_FALSE(settings_set_fakeap_channel(&s, 0));
    TEST_ASSERT_FALSE(settings_set_fakeap_channel(&s, 15));
    TEST_ASSERT_FALSE(settings_set_fakeap_max_connections(&s, 0));
    TEST_ASSERT_FALSE(settings_set_fakeap_max_connections(&s, 9));
    TEST_ASSERT_FALSE(
        settings_set_fakeap_beacon_interval(&s, 10001));

    TEST_ASSERT_TRUE(settings_set_fakeap_channel(&s, 11));
    TEST_ASSERT_EQUAL_UINT8(11, s.fakeap_channel);

    TEST_ASSERT_TRUE(settings_set_idle_timeout_ms(&s, 3000));
    TEST_ASSERT_EQUAL_UINT16(3000, s.idle_timeout_ms);
    TEST_ASSERT_TRUE(settings_set_warning_duration_ms(&s, 3500));
    TEST_ASSERT_EQUAL_UINT16(3500, s.warning_duration_ms);

    /* OFF / safe-mode slot is a valid selection */
    TEST_ASSERT_TRUE(settings_set_default_action(&s, SETTING_ACTION_OFF));
    TEST_ASSERT_EQUAL_INT(SETTING_ACTION_OFF, s.default_action);
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

    /* control-panel AP name: set/reset/validate */
    TEST_ASSERT_FALSE(settings_set_ap_ssid(&s, ""));
    TEST_ASSERT_TRUE(settings_set_ap_ssid(&s, "RedTeam-Net"));
    TEST_ASSERT_EQUAL_STRING("RedTeam-Net", s.ap_ssid);
    /* SSIDs are length-checked (WiFi max is 32 bytes) */
    char long_ssid[SETTINGS_SSID_MAX_LEN + 8];
    memset(long_ssid, 'x', sizeof(long_ssid) - 1);
    long_ssid[sizeof(long_ssid) - 1] = '\0';
    TEST_ASSERT_FALSE(settings_set_ap_ssid(&s, long_ssid));

    char max_ssid[SETTINGS_SSID_MAX_LEN];
    memset(max_ssid, 'x', sizeof(max_ssid) - 1);
    max_ssid[sizeof(max_ssid) - 1] = '\0';
    TEST_ASSERT_TRUE(settings_set_ap_ssid(&s, max_ssid));
    TEST_ASSERT_EQUAL_UINT(31, strlen(s.ap_ssid));

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

void test_action_summary(void) {
    velo_settings_t s;
    settings_init_default(&s);

    char buf[128];
    TEST_ASSERT_TRUE(settings_action_summary(
        &s, SETTING_ACTION_FAKE_AP, buf, sizeof(buf)) > 0);
    TEST_ASSERT_TRUE(strstr(buf, "ch=1") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "conn=4") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "tu=100") != NULL);

    TEST_ASSERT_TRUE(settings_action_summary(
        &s, SETTING_ACTION_BLE_SPAM, buf, sizeof(buf)) > 0);
    TEST_ASSERT_TRUE(strstr(buf, "enabled") != NULL);

    TEST_ASSERT_TRUE(settings_action_summary(
        &s, SETTING_ACTION_DEAUTH, buf, sizeof(buf)) > 0);

    TEST_ASSERT_TRUE(settings_action_summary(
        &s, SETTING_ACTION_OFF, buf, sizeof(buf)) > 0);
    TEST_ASSERT_TRUE(strstr(buf, "safe") != NULL);

    TEST_ASSERT_EQUAL_UINT32(0, settings_action_summary(
        NULL, SETTING_ACTION_BLE_SPAM, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, settings_action_summary(
        &s, SETTING_ACTION_COUNT, buf, sizeof(buf)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_sane);
    RUN_TEST(test_sleep_timeout_bounds);
    RUN_TEST(test_out_of_range_values_rejected);
    RUN_TEST(test_ssid_add_and_bounds);
    RUN_TEST(test_ssids_text_roundtrip);
    RUN_TEST(test_action_summary);
    return UNITY_END();
}