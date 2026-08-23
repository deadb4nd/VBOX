#include "unity.h"
#include <ap_utils.h>

void returns_len_3_for_array_of_length_3(void) {
  const char *arr[] = {"1", "2", "3"};
  int len = 3;
  TEST_ASSERT_EQUAL_INT(len, const_arr_len(arr));
}

void returns_len_1_for_array_of_length_1(void) {
  const char *arr[] = {"1"};
  int len = 1;
  TEST_ASSERT_EQUAL_INT(len, arr_len(arr));
}

void returns_len_1_for_array_of_length_0(void) {
  const char *arr[] = {NULL};
  int len = 1;
  TEST_ASSERT_EQUAL_INT(len, arr_len(arr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(returns_len_3_for_array_of_length_3);
  RUN_TEST(returns_len_1_for_array_of_length_1);
  RUN_TEST(returns_len_1_for_array_of_length_0);
  return UNITY_END();
}
