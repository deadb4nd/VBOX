#include "settings.h"

#include "velobox_config.h"

#include <stdio.h>
#include <string.h>

static const char *const DEFAULT_SSIDS[] = {
    "Click Here for Viruses",
    "Please Connect for Identity Theft",
    "Gone Phishing",
    "I Am Watching You",
    "Virus Upload Complete",
    "NSA Listening Post",
    "We Know Where You Live",
    "Definitely Not a Trap",
    "Malware Distribution Center",
    "No Free Wifi Here",
    "Please Bring Wine",
    "My Neighbors Suck",
    "The Smith Family Wifi",
    "Password is Password",
    "Hey Get Your Own Wifi",
    "The Silence of the LANs",
};

static bool valid_channel(uint8_t ch) { return ch >= 1 && ch <= 14; }
static bool valid_max_conn(uint8_t n) { return n >= 1 && n <= 8; }
static bool valid_interval(uint16_t tu) { return tu >= 20 && tu <= 10000; }
static bool valid_duration(uint16_t ms) { return ms >= 500 && ms <= 10000; }
static bool valid_sleep(uint32_t ms) {
    return ms == 0 || (ms >= 30000 && ms <= 3600000);
}
static bool ssid_ok(const char *ssid);

void settings_init_default(velo_settings_t *s) {
    if (!s) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->default_action = SETTING_ACTION_DEAUTH;
    s->idle_timeout_ms = 1500;
    s->warning_duration_ms = 2000;
    snprintf(s->ap_ssid, SETTINGS_SSID_MAX_LEN, "%s", VELO_DEFAULT_AP_SSID);
    s->fakeap_channel = 1;
    s->fakeap_max_connections = 4;
    s->fakeap_beacon_interval = 100;
    s->ble_spam_enabled = true;
    s->sleep_timeout_ms = VELO_DEFAULT_SLEEP_MS; /* sleep off by default */

    static const size_t default_count =
        sizeof(DEFAULT_SSIDS) / sizeof(DEFAULT_SSIDS[0]);
    for (size_t i = 0; i < default_count && i < SETTINGS_MAX_SSIDS; i++) {
        snprintf(s->ssids[i], SETTINGS_SSID_MAX_LEN, "%s", DEFAULT_SSIDS[i]);
    }
    s->ssid_count = default_count < SETTINGS_MAX_SSIDS ? default_count
                                                        : SETTINGS_MAX_SSIDS;
}

bool settings_set_default_action(velo_settings_t *s, settings_action_t a) {
    if (!s || a >= SETTING_ACTION_COUNT) {
        return false;
    }
    s->default_action = a;
    return true;
}

bool settings_set_idle_timeout_ms(velo_settings_t *s, uint16_t ms) {
    if (!s || !valid_duration(ms)) {
        return false;
    }
    s->idle_timeout_ms = ms;
    return true;
}

bool settings_set_warning_duration_ms(velo_settings_t *s, uint16_t ms) {
    if (!s || !valid_duration(ms)) {
        return false;
    }
    s->warning_duration_ms = ms;
    return true;
}

bool settings_set_ap_ssid(velo_settings_t *s, const char *ssid) {
    if (!s || !ssid_ok(ssid)) {
        return false;
    }
    snprintf(s->ap_ssid, SETTINGS_SSID_MAX_LEN, "%s", ssid);
    return true;
}

bool settings_set_fakeap_channel(velo_settings_t *s, uint8_t ch) {
    if (!s || !valid_channel(ch)) {
        return false;
    }
    s->fakeap_channel = ch;
    return true;
}

bool settings_set_fakeap_max_connections(velo_settings_t *s, uint8_t n) {
    if (!s || !valid_max_conn(n)) {
        return false;
    }
    s->fakeap_max_connections = n;
    return true;
}

bool settings_set_fakeap_beacon_interval(velo_settings_t *s, uint16_t tu) {
    if (!s || !valid_interval(tu)) {
        return false;
    }
    s->fakeap_beacon_interval = tu;
    return true;
}

bool settings_set_ble_spam_enabled(velo_settings_t *s, bool on) {
    if (!s) {
        return false;
    }
    s->ble_spam_enabled = on;
    return true;
}

bool settings_set_sleep_timeout_ms(velo_settings_t *s, uint32_t ms) {
    if (!s || !valid_sleep(ms)) {
        return false;
    }
    s->sleep_timeout_ms = ms;
    return true;
}

uint32_t settings_ssid_count(const velo_settings_t *s) {
    return s ? s->ssid_count : 0;
}

static bool ssid_ok(const char *ssid) {
    if (!ssid) {
        return false;
    }
    size_t len = strlen(ssid);
    return len > 0 && len < SETTINGS_SSID_MAX_LEN;
}

bool settings_add_ssid(velo_settings_t *s, const char *ssid) {
    if (!s || !ssid_ok(ssid) || s->ssid_count >= SETTINGS_MAX_SSIDS) {
        return false;
    }
    snprintf(s->ssids[s->ssid_count], SETTINGS_SSID_MAX_LEN, "%s", ssid);
    s->ssid_count++;
    return true;
}

bool settings_get_ssid(const velo_settings_t *s, uint32_t idx, char *out,
                       size_t out_len) {
    if (!s || !out || out_len == 0 || idx >= s->ssid_count) {
        return false;
    }
    snprintf(out, out_len, "%s", s->ssids[idx]);
    return true;
}

bool settings_set_ssids_text(velo_settings_t *s, const char *text) {
    if (!s || !text) {
        return false;
    }

    uint32_t count = 0;
    const char *p = text;
    while (*p && count < SETTINGS_MAX_SSIDS) {
        const char *start = p;
        while (*p && *p != '\n') {
            p++;
        }
        size_t len = (size_t)(p - start);
        while (len > 0 && (start[len - 1] == '\r' || start[len - 1] == ' ')) {
            len--;
        }
        if (len > 0 && len < SETTINGS_SSID_MAX_LEN) {
            memcpy(s->ssids[count], start, len);
            s->ssids[count][len] = '\0';
            count++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    s->ssid_count = count;
    return true;
}

size_t settings_action_summary(const velo_settings_t *s, settings_action_t a,
                               char *buf, size_t len) {
    if (!s || !buf || len == 0 || a >= SETTING_ACTION_COUNT) {
        return 0;
    }
    buf[0] = '\0';
    switch (a) {
    case SETTING_ACTION_DEAUTH:
        return (size_t)snprintf(buf, len, "no params");
    case SETTING_ACTION_BLE_SPAM:
        return (size_t)snprintf(buf, len, "spam=%s",
                                s->ble_spam_enabled ? "enabled" : "DISABLED");
    case SETTING_ACTION_FAKE_AP:
        return (size_t)snprintf(buf, len, "ch=%u conn=%u tu=%u ssids=%lu",
                                s->fakeap_channel, s->fakeap_max_connections,
                                s->fakeap_beacon_interval,
                                (unsigned long)s->ssid_count);
    case SETTING_ACTION_OFF:
        return (size_t)snprintf(buf, len, "safe mode - will NOT fire");
    default:
        return 0;
    }
}

size_t settings_ssids_to_text(const velo_settings_t *s, char *buf, size_t len) {
    if (!s || !buf || len == 0) {
        return 0;
    }
    size_t used = 0;
    for (uint32_t i = 0; i < s->ssid_count; i++) {
        int written = snprintf(buf + used, len - used, "%s\n", s->ssids[i]);
        if (written < 0) {
            break;
        }
        used += (size_t)written;
        if (used >= len) {
            break;
        }
    }
    return used;
}