#pragma once

/*
 * VeloBox build configuration
 * ===========================
 *
 * This is the ONE file to edit for a custom build. Everything here is
 * compile-time: change a value, rebuild, reflash.
 *
 * For changes that must NOT need a rebuild (product name, accent colour,
 * enabling/disabling tools at runtime), use the Settings tab in the web
 * console instead -- those are stored in NVS.
 *
 * Adding a new tool ("module") is also designed to be easy: see
 * docs/customizing.md for a copy-paste template. Short version:
 *   1. add one X(...) row to VELO_MODULE_LIST in module_list.h
 *   2. add one run_* function (optional feature flag below)
 * The web UI, the API and the script engine all build themselves from the
 * registry, so no HTML/JS edits are ever needed.
 */

/* ------------------------------------------------------------------ *
 *  Hardware pins                                                      *
 * ------------------------------------------------------------------ */

#define VELO_PIN_LED 15      /* status LED (active LOW)                  */
#define VELO_PIN_BUZZER 10   /* piezo buzzer; set to -1 if unused        */
#define VELO_PIN_MOTOR 2     /* vibration motor (haptic feedback)        */
#define VELO_PIN_BALL_BTN 21 /* tilt/ball switch (also the wake source)  */

/* External antenna switch on the XIAO ESP32C6. Set either to -1 if your
   board has no switch. NEVER remove the init on a XIAO: without it the
   radio is effectively deaf. GP32-C6 is a GPIO, not a strapping pin. */
#define VELO_PIN_ANT_SEL 3
#define VELO_PIN_ANT_EN 14

/* ------------------------------------------------------------------ *
 *  Branding (defaults; the app name/colour can also be set at runtime  *
 *  from the Settings tab once Phase 2 lands)                           *
 * ------------------------------------------------------------------ */

#define VELO_BRAND_NAME "VeloBox"
#define VELO_BRAND_TAGLINE "Wireless Assessment Console"

/* ------------------------------------------------------------------ *
 *  Feature flags                                                      *
 *  1 = build it in, register it in the UI and allow it to run         *
 *  0 = hide it from the UI and refuse to start it                     *
 * ------------------------------------------------------------------ */

#define VELO_ENABLE_DEAUTH 1   /* WiFi deauth / disassociation flood     */
#define VELO_ENABLE_BLE_SPAM 1 /* BLE advertising flood                  */
#define VELO_ENABLE_FAKE_AP 1  /* cloned-SSID beacon flood               */
#define VELO_ENABLE_RECON 1    /* WiFi AP/client monitor + handshake     */
#define VELO_ENABLE_BLE_SCAN 1 /* BLE GAP discovery                      */
#define VELO_ENABLE_PROBE 1    /* probe-request flood                    */

/* ------------------------------------------------------------------ *
 *  First-boot defaults (only used when NVS is empty / erased)          *
 * ------------------------------------------------------------------ */

#define VELO_DEFAULT_AP_SSID VELO_BRAND_NAME
#define VELO_DEFAULT_SLEEP_MS 0u /* 0 = never; else 30000..3600000        */

/* Sleep behaviour (plantable mode). The C6 cannot deep-sleep-wake on
   GPIO21, so we use light sleep + level wake on the ball pin. */
#define VELO_SLEEP_KEEPALIVE_US (30LL * 60 * 1000000) /* 30 min safety net */
