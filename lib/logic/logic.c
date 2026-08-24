#include "logic.h"
#include <esp_timer.h>

#define DEBOUNCE_US 200000 // 200 ms — tweak by feel

static int64_t last_accepted_us = 0;

bool has_ball_moved(int ball_value, int last_value) {
  if (ball_value == last_value) {
    return false; // no electrical change at all
  }

  int64_t now = esp_timer_get_time();
  if ((now - last_accepted_us) < DEBOUNCE_US) {
    return false; // still bouncing — ignore
  }

  last_accepted_us = now;
  return true; // one clean tick
}

void update(int *selection) {
  *selection += 1;
  if (*selection >= 3) {
    *selection = 0;
  }
}