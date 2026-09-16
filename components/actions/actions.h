#pragma once

#include "settings.h"
#include <stdbool.h>

typedef enum {
    ACTION_NONE = 0,
    ACTION_WIFI_DEAUTH,
    ACTION_BLE_SPAM,
    ACTION_FAKE_AP,
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

bool actions_is_running(void);
action_t actions_current(void);