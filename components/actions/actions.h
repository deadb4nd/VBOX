#pragma once

#include "settings.h"
#include "script.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* UI grouping for a module. The web console renders one tile grid per
   group, in registry order. */
#define VELO_GROUP_OPERATIONS 0 /* disruptive / attacking tools */
#define VELO_GROUP_RECON 1      /* passive discovery tools      */

/* The single source of truth. module_list.h holds the VELO_MODULE_LIST(X)
   table; here we expand it into the ACTION_* enum. Add a tool there, not
   here. */
#include "module_list.h"

typedef enum {
    ACTION_NONE = 0,
#define VELO_MODULE_ENUM(id, slug, label, hint, group, danger, flag, run) id,
    VELO_MODULE_LIST(VELO_MODULE_ENUM)
#undef VELO_MODULE_ENUM
    ACTION_COUNT
} action_t;

/* One entry per module, generated from module_list.h. Everything the
   console and the action engine need lives here. */
typedef struct {
    action_t action;              /* internal id                      */
    const char *slug;             /* stable API/UI id, e.g. "deauth"  */
    const char *label;            /* human name, e.g. "WiFi Deauth"   */
    const char *hint;             /* one-line description for the UI  */
    uint8_t group;                /* VELO_GROUP_*                     */
    bool danger;                  /* destructive -> danger styling    */
    uint8_t flag;                 /* VELO_ENABLE_* (0 = compiled out) */
    void (*run)(volatile bool *kill); /* the tool's body             */
} velo_module_t;

/* The actions component reads settings from this struct. Must be
   called once after settings have been loaded. */
void actions_set_settings(velo_settings_t *s);

const char *actions_name(action_t a);

/* ---- module registry ---- */
/* All modules compiled in, in UI order. `*count` receives the length. */
const velo_module_t *actions_modules(size_t *count);
const velo_module_t *actions_module_at(size_t i);
const velo_module_t *actions_module_for(action_t a);
const velo_module_t *actions_module_by_slug(const char *slug);
/* Registry id for an action ("" if unknown, "none" for ACTION_NONE). */
const char *actions_slug(action_t a);
/* Enabled = compiled in AND permitted by current settings (e.g. BLE spam
   may be switched off). */
bool actions_module_enabled(const velo_module_t *m);

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

/* Snapshot the currently-running script: copies its steps into `out` and
   reports the 0-based index of the step being executed right now. Returns
   false (leaving `out`/`index` untouched) when no script is running. */
bool actions_script_snapshot(script_t *out, uint32_t *index);

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