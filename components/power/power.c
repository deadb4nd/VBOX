#include "power.h"

static int64_t s_last_us = 0;
static bool s_have_activity = false;

void power_reset(void) {
    s_last_us = 0;
    s_have_activity = false;
}

void power_note_activity(int64_t now_us) {
    s_last_us = now_us;
    s_have_activity = true;
}

int64_t power_idle_ms(int64_t now_us) {
    if (!s_have_activity || now_us < s_last_us) {
        return 0;
    }
    return (now_us - s_last_us) / 1000;
}

bool power_should_sleep(int64_t now_us, uint32_t timeout_ms) {
    if (timeout_ms == 0 || !s_have_activity) {
        return false;
    }
    return power_idle_ms(now_us) >= (int64_t)timeout_ms;
}
