#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SETTINGS_MAX_SSIDS 32
#define SETTINGS_SSID_MAX_LEN 32

typedef enum {
    SETTING_ACTION_DEAUTH = 0,
    SETTING_ACTION_BLE_SPAM,
    SETTING_ACTION_FAKE_AP,
    SETTING_ACTION_OFF, /* safe/idle slot: never auto-fires */
    SETTING_ACTION_COUNT
} settings_action_t;

typedef struct {
    settings_action_t default_action;      /* ball UI idle selection            */
    uint16_t idle_timeout_ms;              /* 500..10000, arm delay after roll  */
    uint16_t warning_duration_ms;          /* 500..10000, confirm window        */
    char ap_ssid[SETTINGS_SSID_MAX_LEN];   /* control-panel WiFi name (1..31)    */
    uint8_t fakeap_channel;                /* 1..14                            */
    uint8_t fakeap_max_connections;        /* 1..8                             */
    uint16_t fakeap_beacon_interval;       /* TU (1 TU = 1024 us)               */
    bool ble_spam_enabled;                 /* allow BLE spam action             */
    char ssids[SETTINGS_MAX_SSIDS][SETTINGS_SSID_MAX_LEN];
    uint32_t ssid_count;
} velo_settings_t;

void settings_init_default(velo_settings_t *s);

bool settings_set_default_action(velo_settings_t *s, settings_action_t a);
bool settings_set_idle_timeout_ms(velo_settings_t *s, uint16_t ms);
bool settings_set_warning_duration_ms(velo_settings_t *s, uint16_t ms);
bool settings_set_ap_ssid(velo_settings_t *s, const char *ssid);
bool settings_set_fakeap_channel(velo_settings_t *s, uint8_t ch);
bool settings_set_fakeap_max_connections(velo_settings_t *s, uint8_t n);
bool settings_set_fakeap_beacon_interval(velo_settings_t *s, uint16_t tu);
bool settings_set_ble_spam_enabled(velo_settings_t *s, bool on);

uint32_t settings_ssid_count(const velo_settings_t *s);
bool settings_add_ssid(velo_settings_t *s, const char *ssid);
bool settings_get_ssid(const velo_settings_t *s, uint32_t idx, char *out,
                       size_t out_len);
/* Fills the list from newline ('\n') separated text. Excess entries ignored. */
bool settings_set_ssids_text(velo_settings_t *s, const char *text);
size_t settings_ssids_to_text(const velo_settings_t *s, char *buf, size_t len);

/* Human-readable line describing the settings that apply to one action,
   e.g. "spam=enabled" or "ch=1 conn=4 tu=100 ssids=16". Returns bytes
   written (0 on bad input). */
size_t settings_action_summary(const velo_settings_t *s, settings_action_t a,
                               char *buf, size_t len);