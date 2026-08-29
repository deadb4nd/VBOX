#include "unity.h"
#include <ap_config.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static ap_config_t blank_config(void) {
    ap_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    return cfg;
}
// REDO TESTS!!! THEY DONT' WORK ANYUMORE LOL``

// ---------------------- WIFI_CHANNEL ----------------------

void test_channel_defaults_to_2_when_zero(void) {
    ap_config_t config = blank_config();
    config.WIFI_CHANNEL = 0;

    create_config(&config);

    TEST_ASSERT_EQUAL(2, config.WIFI_CHANNEL);
}

void test_channel_is_preserved_when_nonzero(void) {
    ap_config_t config = blank_config();
    config.WIFI_CHANNEL = 11;

    create_config(&config);

    TEST_ASSERT_EQUAL(11, config.WIFI_CHANNEL);
}

// -------------------- MAX_CONNECTIONS --------------------

void test_max_connections_defaults_to_4_when_zero(void) {
    ap_config_t config = blank_config();
    config.MAX_CONNECTIONS = 0;

    create_config(&config);

    TEST_ASSERT_EQUAL(4, config.MAX_CONNECTIONS);
}

void test_max_connections_is_preserved_when_nonzero(void) {
    ap_config_t config = blank_config();
    config.MAX_CONNECTIONS = 8;

    create_config(&config);

    TEST_ASSERT_EQUAL(8, config.MAX_CONNECTIONS);
}

// ------------------------- SSID -------------------------

void test_ssid_defaults_when_empty(void) {
    ap_config_t config = blank_config();

    create_config(&config);

    TEST_ASSERT_EQUAL_STRING("ESP32C6 WiFi", (const char *)config.SSID);
}

void test_ssid_is_preserved_when_provided(void) {
    ap_config_t config = blank_config();
    const char *name = "MyNetwork";
    memcpy(config.SSID, name, strlen(name) + 1);

    create_config(&config);

    TEST_ASSERT_EQUAL_STRING("MyNetwork", (const char *)config.SSID);
}

void test_ssid_full_length_bytes_are_left_untouched(void) {
    ap_config_t config = blank_config();
    memset(config.SSID, 'A', sizeof(config.SSID));

    ap_config_t expected = config;
    create_config(&config);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.SSID, config.SSID,
                                  sizeof(config.SSID));
}

// ----------------------- PASSWORD -----------------------

void test_password_is_zeroed_when_empty(void) {
    ap_config_t config = blank_config();

    create_config(&config);

    TEST_ASSERT_EACH_EQUAL_UINT8(0, config.PASSWORD, sizeof(config.PASSWORD));
}

void test_password_is_preserved_when_provided(void) {
    ap_config_t config = blank_config();
    const char *pw = "SuperSecret1";
    memcpy(config.PASSWORD, pw, strlen(pw) + 1);

    create_config(&config);

    TEST_ASSERT_EQUAL_STRING("SuperSecret1", (const char *)config.PASSWORD);
}

void test_password_up_to_63_chars_fits_without_truncation(void) {
    ap_config_t config = blank_config();
    const char *pw =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456";
    // 60 chars btw
    TEST_ASSERT_TRUE(strlen(pw) < AP_PASSWORD_MAX_LEN);
    memcpy(config.PASSWORD, pw, strlen(pw) + 1);

    create_config(&config);

    TEST_ASSERT_EQUAL_STRING(pw, (const char *)config.PASSWORD);
}

/* -------------------- Combined / integration -------------------- */

void test_all_defaults_applied_for_fully_blank_input(void) {
    ap_config_t config = blank_config();

    create_config(&config);

    TEST_ASSERT_EQUAL(2, config.WIFI_CHANNEL);
    TEST_ASSERT_EQUAL(4, config.MAX_CONNECTIONS);
    TEST_ASSERT_EQUAL_STRING("ESP32C6 WiFi", (const char *)config.SSID);
    TEST_ASSERT_EACH_EQUAL_UINT8(0, config.PASSWORD, sizeof(config.PASSWORD));
}

void test_all_custom_values_are_preserved_together(void) {
    ap_config_t config = blank_config();
    config.WIFI_CHANNEL = 6;
    config.MAX_CONNECTIONS = 10;
    memcpy(config.SSID, "CustomAP", strlen("CustomAP") + 1);
    memcpy(config.PASSWORD, "hunter2pass", strlen("hunter2pass") + 1);

    create_config(&config);

    TEST_ASSERT_EQUAL(6, config.WIFI_CHANNEL);
    TEST_ASSERT_EQUAL(10, config.MAX_CONNECTIONS);
    TEST_ASSERT_EQUAL_STRING("CustomAP", (const char *)config.SSID);
    TEST_ASSERT_EQUAL_STRING("hunter2pass", (const char *)config.PASSWORD);
}

void test_mixed_some_default_some_custom(void) {
    ap_config_t config = blank_config();
    memcpy(config.SSID, "MixedAP", strlen("MixedAP") + 1);
    memcpy(config.PASSWORD, "mixedpw123", strlen("mixedpw123") + 1);

    create_config(&config);

    TEST_ASSERT_EQUAL(2, config.WIFI_CHANNEL);
    TEST_ASSERT_EQUAL(4, config.MAX_CONNECTIONS);
    TEST_ASSERT_EQUAL_STRING("MixedAP", (const char *)config.SSID);
    TEST_ASSERT_EQUAL_STRING("mixedpw123", (const char *)config.PASSWORD);
}

void test_create_config_is_idempotent(void) {
    ap_config_t config = blank_config();

    create_config(&config);
    ap_config_t after_first = config;

    create_config(&config);

    TEST_ASSERT_EQUAL(after_first.WIFI_CHANNEL, config.WIFI_CHANNEL);
    TEST_ASSERT_EQUAL(after_first.MAX_CONNECTIONS, config.MAX_CONNECTIONS);
    TEST_ASSERT_EQUAL_STRING((const char *)after_first.SSID,
                             (const char *)config.SSID);
    TEST_ASSERT_EQUAL_STRING((const char *)after_first.PASSWORD,
                             (const char *)config.PASSWORD);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_channel_defaults_to_2_when_zero);
    RUN_TEST(test_channel_is_preserved_when_nonzero);

    RUN_TEST(test_max_connections_defaults_to_4_when_zero);
    RUN_TEST(test_max_connections_is_preserved_when_nonzero);

    RUN_TEST(test_ssid_defaults_when_empty);
    RUN_TEST(test_ssid_is_preserved_when_provided);
    RUN_TEST(test_ssid_full_length_bytes_are_left_untouched);

    RUN_TEST(test_password_is_zeroed_when_empty);
    RUN_TEST(test_password_is_preserved_when_provided);
    RUN_TEST(test_password_up_to_63_chars_fits_without_truncation);

    RUN_TEST(test_all_defaults_applied_for_fully_blank_input);
    RUN_TEST(test_all_custom_values_are_preserved_together);
    RUN_TEST(test_mixed_some_default_some_custom);
    RUN_TEST(test_create_config_is_idempotent);

    return UNITY_END();
}
