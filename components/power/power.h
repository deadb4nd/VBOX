#pragma once

/* Inactivity accounting for the deep-sleep policy. Pure C so the policy can
   be tested on the host; the caller supplies the clock (main passes
   esp_timer_get_time()). A timeout of 0 disables sleeping entirely. */

#include <stdbool.h>
#include <stdint.h>

void power_reset(void);

/* Record user/network activity at `now_us`. */
void power_note_activity(int64_t now_us);

/* Milliseconds since the last recorded activity (0 before the first one). */
int64_t power_idle_ms(int64_t now_us);

/* True when we have seen activity and have been idle for at least
   `timeout_ms`. `timeout_ms == 0` never sleeps. */
bool power_should_sleep(int64_t now_us, uint32_t timeout_ms);
