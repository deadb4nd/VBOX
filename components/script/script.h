#pragma once

/* Tiny scripting language that turns the fixed action buttons into
   programmable attack sequences. Pure C so the parser is host-testable.

   One step per line, e.g.

       # grab handshakes across the band
       recon ap=aa:bb:cc:dd:ee:ff ms=15000
       deauth aa:bb:cc:dd:ee:ff 11:22:33:44:55:66 4000
       wait 2000

   Grammar: `command [ap=MAC] [client=MAC] [ms=N]`. A bare MAC becomes the
   AP (or the client if the AP is already set) and a bare number is the
   duration. Unknown lines are skipped, so a typo costs one step, never
   the whole script. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCRIPT_MAX_STEPS 16
#define SCRIPT_MAX_TEXT 1024
#define SCRIPT_DEFAULT_MS 5000
#define SCRIPT_MIN_MS 100
#define SCRIPT_MAX_MS 600000
#define SCRIPT_SLUG_MAX 20

typedef enum {
    SCRIPT_CMD_NONE = 0,
    SCRIPT_CMD_DEAUTH,   /* targeted/broadcast deauth */
    SCRIPT_CMD_RECON,    /* promiscuous scan (also harvests handshakes) */
    SCRIPT_CMD_BLE_SCAN, /* GAP discovery */
    SCRIPT_CMD_BLE_SPAM, /* BLE advert flood (honours settings) */
    SCRIPT_CMD_PROBE,    /* probe-request flood */
    SCRIPT_CMD_FAKE_AP,  /* beacon spam / rogue AP */
    SCRIPT_CMD_WAIT,     /* do nothing, just burn the duration */
    SCRIPT_CMD_ACTION,   /* run any module by slug: "action <slug> [ms=]" */
    SCRIPT_CMD_COUNT
} script_cmd_t;

typedef struct {
    script_cmd_t cmd;
    char slug[SCRIPT_SLUG_MAX]; /* SCRIPT_CMD_ACTION: module id */
    uint32_t duration_ms;
    bool has_ap;
    uint8_t ap[6];
    bool has_client;
    uint8_t client[6];
} script_step_t;

typedef struct {
    script_step_t steps[SCRIPT_MAX_STEPS];
    uint32_t count;
} script_t;

const char *script_cmd_name(script_cmd_t c);
bool script_cmd_from_name(const char *name, script_cmd_t *out);

/* Display name of a step: the module slug for an "action <slug>" step,
   otherwise the fixed command name. */
const char *script_step_name(const script_step_t *st);

/* Accepts "aabbccddeeff", "aa:bb:cc:dd:ee:ff" and "aa-bb-cc-dd-ee-ff". */
bool script_parse_mac(const char *s, uint8_t out[6]);

/* Parse newline separated text. Returns number of steps written. A result of
   0 means nothing runnable (empty text or only comments/errors). */
uint32_t script_parse(const char *text, script_t *out);

/* Serialize back to canonical text. Returns bytes written. */
size_t script_to_text(const script_t *s, char *buf, size_t len);

/* Sum of all step durations (estimated wall-clock runtime). */
uint32_t script_total_ms(const script_t *s);
