#include "script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct cmd_entry {
    const char *name;
    script_cmd_t cmd;
};

static const struct cmd_entry CMDS[] = {
    {"deauth", SCRIPT_CMD_DEAUTH},   {"recon", SCRIPT_CMD_RECON},
    {"scan", SCRIPT_CMD_RECON},      {"blescan", SCRIPT_CMD_BLE_SCAN},
    {"ble_scan", SCRIPT_CMD_BLE_SCAN},
    {"blespam", SCRIPT_CMD_BLE_SPAM},
    {"ble", SCRIPT_CMD_BLE_SPAM},    {"probe", SCRIPT_CMD_PROBE},
    {"fakeap", SCRIPT_CMD_FAKE_AP},  {"fake_ap", SCRIPT_CMD_FAKE_AP},
    {"wait", SCRIPT_CMD_WAIT},       {"sleep", SCRIPT_CMD_WAIT},
    /* generic: runs any module registered in the firmware by its slug,
       so new tools are scriptable without touching this file */
    {"action", SCRIPT_CMD_ACTION},   {"module", SCRIPT_CMD_ACTION},
    {"run", SCRIPT_CMD_ACTION},
};

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool script_parse_mac(const char *s, uint8_t out[6]) {
    if (!s || !out) {
        return false;
    }
    int nibbles = 0;
    uint8_t acc = 0;
    for (const char *p = s; *p; p++) {
        if (*p == ':' || *p == '-') {
            continue; /* separators are decorative, position is implied */
        }
        int v = hexval((unsigned char)*p);
        if (v < 0) {
            return false;
        }
        acc = (uint8_t)((acc << 4) | (uint8_t)v);
        if ((++nibbles & 1) == 0) {
            if (nibbles / 2 > 6) {
                return false;
            }
            out[nibbles / 2 - 1] = acc;
        }
    }
    return nibbles == 12;
}

static int cmd_index(const char *name) {
    for (size_t i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (strcasecmp(name, CMDS[i].name) == 0) {
            return CMDS[i].cmd;
        }
    }
    return -1;
}

const char *script_cmd_name(script_cmd_t c) {
    switch (c) {
    case SCRIPT_CMD_DEAUTH:
        return "deauth";
    case SCRIPT_CMD_RECON:
        return "recon";
    case SCRIPT_CMD_BLE_SCAN:
        return "blescan";
    case SCRIPT_CMD_BLE_SPAM:
        return "blespam";
    case SCRIPT_CMD_PROBE:
        return "probe";
    case SCRIPT_CMD_FAKE_AP:
        return "fakeap";
    case SCRIPT_CMD_WAIT:
        return "wait";
    case SCRIPT_CMD_ACTION:
        return "action";
    default:
        return "none";
    }
}

const char *script_step_name(const script_step_t *st) {
    if (st->cmd == SCRIPT_CMD_ACTION && st->slug[0]) {
        return st->slug;
    }
    return script_cmd_name(st->cmd);
}

bool script_cmd_from_name(const char *name, script_cmd_t *out) {
    if (!name || !out) {
        return false;
    }
    int c = cmd_index(name);
    if (c < 0) {
        return false;
    }
    *out = (script_cmd_t)c;
    return true;
}

static bool token_is_number(const char *t) {
    if (!*t) {
        return false;
    }
    for (const char *p = t; *p; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}

/* Does this token look like a MAC (12 hex chars, separators allowed)? */
static bool token_is_mac(const char *t) {
    uint8_t tmp[6];
    if (!strchr(t, ':') && !strchr(t, '-') && strlen(t) != 12) {
        return false;
    }
    return script_parse_mac(t, tmp);
}

static uint32_t clamp_ms(uint32_t ms) {
    if (ms < SCRIPT_MIN_MS) return SCRIPT_MIN_MS;
    if (ms > SCRIPT_MAX_MS) return SCRIPT_MAX_MS;
    return ms;
}

/* Copy the next whitespace-delimited token into tok (NUL terminated). */
static const char *next_token(const char *p, char *tok, size_t cap) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    size_t i = 0;
    while (*p && !isspace((unsigned char)*p)) {
        if (i + 1 < cap) {
            tok[i++] = *p;
        }
        p++;
    }
    tok[i] = '\0';
    return p;
}

uint32_t script_parse(const char *text, script_t *out) {
    if (!out) {
        return 0;
    }
    out->count = 0;
    if (!text) {
        return 0;
    }

    const char *line = text;
    while (*line && out->count < SCRIPT_MAX_STEPS) {
        const char *eol = strchr(line, '\n');
        size_t line_len = eol ? (size_t)(eol - line) : strlen(line);

        /* work on a bounded, NUL terminated copy of this line */
        char buf[128];
        if (line_len >= sizeof(buf)) {
            line_len = sizeof(buf) - 1;
        }
        memcpy(buf, line, line_len);
        buf[line_len] = '\0';

        if (eol) {
            line = eol + 1;
        } else {
            line += line_len;
        }

        char *s = buf;
        while (*s && isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == '\0' || *s == '#') {
            continue; /* blank or comment */
        }

        script_step_t step;
        memset(&step, 0, sizeof(step));

        char tok[40];
        const char *p = next_token(s, tok, sizeof(tok));
        int cmd = cmd_index(tok);
        if (cmd <= SCRIPT_CMD_NONE) {
            continue; /* unknown command: skip line */
        }
        step.cmd = (script_cmd_t)cmd;
        step.duration_ms = SCRIPT_DEFAULT_MS;

        while (*p) {
            p = next_token(p, tok, sizeof(tok));
            if (tok[0] == '\0') {
                break;
            }

            char *eq = strchr(tok, '=');
            if (eq) {
                *eq = '\0';
                const char *key = tok;
                const char *val = eq + 1;
                if (strcasecmp(key, "ap") == 0) {
                    if (script_parse_mac(val, step.ap)) {
                        step.has_ap = true;
                    }
                } else if (strcasecmp(key, "client") == 0 ||
                           strcasecmp(key, "sta") == 0) {
                    if (script_parse_mac(val, step.client)) {
                        step.has_client = true;
                    }
                } else if (strcasecmp(key, "ms") == 0 ||
                           strcasecmp(key, "dur") == 0 ||
                           strcasecmp(key, "duration") == 0) {
                    if (token_is_number(val)) {
                        step.duration_ms = clamp_ms((uint32_t)strtoul(val, NULL, 10));
                    }
                }
                continue;
            }

            /* "action <slug>": first bare non-MAC, non-number token is the
               module id, resolved against the firmware registry at run time */
            if (step.cmd == SCRIPT_CMD_ACTION && step.slug[0] == '\0' &&
                !token_is_mac(tok) && !token_is_number(tok)) {
                size_t sl = strlen(tok);
                if (sl > sizeof(step.slug) - 1) {
                    sl = sizeof(step.slug) - 1;
                }
                for (size_t k = 0; k < sl; k++) {
                    step.slug[k] = (char)tolower((unsigned char)tok[k]);
                }
                step.slug[sl] = '\0';
                continue;
            }

            if (token_is_mac(tok)) {
                uint8_t mac[6];
                (void)script_parse_mac(tok, mac);
                if (!step.has_ap) {
                    memcpy(step.ap, mac, 6);
                    step.has_ap = true;
                } else if (!step.has_client) {
                    memcpy(step.client, mac, 6);
                    step.has_client = true;
                }
            } else if (token_is_number(tok)) {
                step.duration_ms = clamp_ms((uint32_t)strtoul(tok, NULL, 10));
            }
        }

        /* deauth without a target is the broadcast case (has_ap stays off) */
        out->steps[out->count++] = step;
    }

    return out->count;
}

/* The command as it should be written back to text. */
static void step_label(const script_step_t *st, char *out, size_t cap) {
    if (st->cmd == SCRIPT_CMD_ACTION) {
        snprintf(out, cap, "action %s", st->slug);
    } else {
        snprintf(out, cap, "%s", script_cmd_name(st->cmd));
    }
}

size_t script_to_text(const script_t *s, char *buf, size_t len) {
    if (!s || !buf || len == 0) {
        return 0;
    }
    size_t w = 0;
    for (uint32_t i = 0; i < s->count; i++) {
        const script_step_t *st = &s->steps[i];
        char label[32];
        step_label(st, label, sizeof(label));
        int n;
        if (st->has_ap && st->has_client) {
            n = snprintf(buf + w, len - w,
                         "%s %02x:%02x:%02x:%02x:%02x:%02x "
                         "%02x:%02x:%02x:%02x:%02x:%02x %lu\n",
                         label, st->ap[0], st->ap[1],
                         st->ap[2], st->ap[3], st->ap[4], st->ap[5],
                         st->client[0], st->client[1], st->client[2],
                         st->client[3], st->client[4], st->client[5],
                         (unsigned long)st->duration_ms);
        } else if (st->has_ap) {
            n = snprintf(buf + w, len - w, "%s ap=%02x:%02x:%02x:%02x:%02x:%02x ms=%lu\n",
                         label, st->ap[0], st->ap[1],
                         st->ap[2], st->ap[3], st->ap[4], st->ap[5],
                         (unsigned long)st->duration_ms);
        } else {
            n = snprintf(buf + w, len - w, "%s %lu\n", label,
                         (unsigned long)st->duration_ms);
        }
        if (n < 0) {
            break;
        }
        w += (size_t)n;
        if (w >= len) {
            break;
        }
    }
    return w;
}

uint32_t script_total_ms(const script_t *s) {
    if (!s) {
        return 0;
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < s->count; i++) {
        total += s->steps[i].duration_ms;
    }
    return total;
}
