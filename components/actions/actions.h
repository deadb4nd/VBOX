#pragma once

#include "settings.h"
#include "script.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACTION_NONE = 0,
    ACTION_WIFI_DEAUTH,
    ACTION_BLE_SPAM,
    ACTION_FAKE_AP,
    ACTION_RECON,       /* WiFi AP/stations scan + deauth/EAPOL counters */
    ACTION_BLE_SCAN,    /* BLE GAP discovery */
    ACTION_PROBE_FLOOD, /* probe-request flood */
    ACTION_COUNT
} action_t;

/* The actions component reads settings from this struct. Must be
   called once after settings have been loaded. */
void actions_set_settings(velo_settings_t *s);

const char *actions_name(action_t a);

/* Start running `a` in a background task. Returns false if another
   action is already running (call actions_stop() first) or the action
   is disabled. */
bool actions_start(action_t a);

/* Ask the running action to stop and wait for it to finish. */
void actions_stop(void);

/* Run a parsed script: each step starts its action, waits the step
   duration, then cleanly stops it before moving on. Returns false if an
   action or another script is already running, or the script is empty. */
bool actions_start_script(const script_t *s);
bool actions_script_running(void);

bool actions_is_running(void);
action_t actions_current(void);

/* Optional attack target: a 6-byte AP BSSID and (optionally) a client
   MAC. Used by broadcast deauth (BSSID only) or targeted deauth (both).
   Set before calling actions_start(ACTION_WIFI_DEAUTH). */
void actions_set_target(const uint8_t ap_bssid[6], const uint8_t client[6]);
void actions_clear_target(void);

/* Runtime counters (reset when the relevant action starts). */
typedef struct {
    uint32_t deauth_sent;
    uint32_t beacon_sent;
    uint32_t probe_sent;
} action_counters_t;

action_counters_t actions_counters(void);