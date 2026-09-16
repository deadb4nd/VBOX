#include "unity.h"
#include <logic.h>

enum ball_state {
    moved = 1,
    not_moved = 0,
};

void test_has_ball_moved(void) {
    TEST_ASSERT_TRUE(has_ball_moved(moved, not_moved));
}

void test_ball_never_moved(void) {
    TEST_ASSERT_FALSE(has_ball_moved(not_moved, not_moved));
}

void test_ball_never_came_back(void) {
    TEST_ASSERT_FALSE(has_ball_moved(moved, moved));
}

void test_update_selection(void) {
    int curr_selection = 0;

    update(&curr_selection);
    TEST_ASSERT_EQUAL_INT(1, curr_selection);

    update(&curr_selection);
    TEST_ASSERT_EQUAL_INT(2, curr_selection);

    /* exactly 3 actions -> wraps back to 0 */
    update(&curr_selection);
    TEST_ASSERT_EQUAL_INT(0, curr_selection);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_has_ball_moved);
    RUN_TEST(test_ball_never_moved);
    RUN_TEST(test_ball_never_came_back);
    RUN_TEST(test_update_selection);
    return UNITY_END();
}
