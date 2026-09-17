#include "unity.h"
#include "power.h"

void setUp(void) { power_reset(); }
void tearDown(void) {}

void test_no_sleep_before_any_activity(void) {
    /* Even with a huge timeout we must not sleep until activity is seen. */
    TEST_ASSERT_FALSE(power_should_sleep(1000000000LL, 60000));
    TEST_ASSERT_EQUAL_INT64(0, power_idle_ms(1000000000LL));
}

void test_idle_grows_after_activity(void) {
    power_note_activity(1000000); /* 1 s */
    TEST_ASSERT_EQUAL_INT64(0, power_idle_ms(1000000));
    TEST_ASSERT_EQUAL_INT64(500, power_idle_ms(1500000));
    TEST_ASSERT_EQUAL_INT64(5000, power_idle_ms(6000000));
}

void test_zero_timeout_never_sleeps(void) {
    power_note_activity(0);
    TEST_ASSERT_FALSE(power_should_sleep(1000000000LL, 0));
}

void test_sleep_boundary(void) {
    power_note_activity(0);
    /* 30 s timeout: 29.999 s no, 30 s yes */
    TEST_ASSERT_FALSE(power_should_sleep(29999000LL, 30000));
    TEST_ASSERT_TRUE(power_should_sleep(30000000LL, 30000));
    TEST_ASSERT_TRUE(power_should_sleep(45000000LL, 30000));
}

void test_activity_resets_timer(void) {
    power_note_activity(0);
    TEST_ASSERT_FALSE(power_should_sleep(29999000LL, 30000));
    power_note_activity(30000000LL); /* user moved again */
    TEST_ASSERT_FALSE(power_should_sleep(45000000LL, 30000));
    TEST_ASSERT_TRUE(power_should_sleep(60000000LL, 30000));
}

void test_clock_going_backwards_is_safe(void) {
    power_note_activity(5000000);
    TEST_ASSERT_EQUAL_INT64(0, power_idle_ms(1000000));
    TEST_ASSERT_FALSE(power_should_sleep(1000000, 30000));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_sleep_before_any_activity);
    RUN_TEST(test_idle_grows_after_activity);
    RUN_TEST(test_zero_timeout_never_sleeps);
    RUN_TEST(test_sleep_boundary);
    RUN_TEST(test_activity_resets_timer);
    RUN_TEST(test_clock_going_backwards_is_safe);
    return UNITY_END();
}
