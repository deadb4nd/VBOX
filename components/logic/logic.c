#include "logic.h"

#define DEBOUNCE_US 200000 // 200 ms — tweak by feel

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#define NOW_US() esp_timer_get_time()
#else
/* Host (native test) build: fake a clock that always ticks past the
   debounce window, so edge-detection tests behave like on device. */
static int64_t host_now_us(void) {
  static int64_t t = 0;
  t += DEBOUNCE_US + 1;
  return t;
}
#define NOW_US() host_now_us()
#endif

static int64_t last_accepted_us = 0;

bool has_ball_moved(int ball_value, int last_value) {
  if (ball_value == last_value) {
    return false; // no electrical change at all
  }

  int64_t now = NOW_US();
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