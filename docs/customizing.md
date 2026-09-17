# Customizing VeloBox

VeloBox is built to be forked and bent to your own needs. Most changes are a
one-line edit, and the web console builds its own buttons from the firmware —
so adding a tool does **not** mean touching HTML or JavaScript.

There are three layers, from "no tools needed" to "write some C":

| Layer | What you can change | Where | Needs a rebuild? |
|-------|---------------------|-------|------------------|
| Runtime | AP name, default action, BLE spam on/off, sleep timeout, fake-AP SSIDs, targeting | web **Settings** tab | no |
| Build config | pins, branding, feature flags, first-boot defaults | `include/velobox_config.h` | yes |
| Source | add a whole new tool ("module") | `components/actions/` | yes |

---

## 1. Build configuration — `include/velobox_config.h`

This is the single file most people should edit.

```c
#define VELO_PIN_LED 15        /* status LED (active LOW)      */
#define VELO_PIN_BUZZER 10     /* set to -1 if unused          */
#define VELO_PIN_MOTOR 2
#define VELO_PIN_BALL_BTN 21   /* tilt switch / wake source    */

#define VELO_BRAND_NAME "VeloBox"
#define VELO_BRAND_TAGLINE "Wireless Assessment Console"

#define VELO_ENABLE_DEAUTH 1
#define VELO_ENABLE_BLE_SPAM 1
#define VELO_ENABLE_FAKE_AP 1
#define VELO_ENABLE_RECON 1
#define VELO_ENABLE_BLE_SCAN 1
#define VELO_ENABLE_PROBE 1
```

* **Pins** — change these to match your board. `VELO_PIN_ANT_SEL` /
  `VELO_PIN_ANT_EN` are the XIAO ESP32-C6 antenna switch; set them to `-1`
  on a board without one. On the XIAO you must keep the antenna init or the
  radio goes deaf.
* **Branding** — the product name and tagline shown in the console header and
  browser tab.
* **Feature flags** — `0` removes a tool from the UI and refuses to start it
  (the C code stays linked, so you can flip it back without hunting through
  files).

After editing, rebuild and reflash (see §5).

---

## 2. Adding a new tool (module)

A "module" is one button/tool. **Adding one is two edits, in two files —
one line of registration plus one function.** The web UI, the `/api/action`
endpoint, the enable/disable logic, the tile order and the script engine all
read from the registry, so there is nothing to change in HTML or JS.

### Step 1 — list it (`components/actions/module_list.h`)

Add one `X(...)` line to `VELO_MODULE_LIST`:

```c
#define VELO_MODULE_LIST(X)                                                  \
    X(ACTION_WIFI_DEAUTH, "deauth", "WiFi Deauth", ...)                      \
    ...
    X(ACTION_MY_TOOL, "mytool", "My Tool",                                   \
      "One-line description under the button", VELO_GROUP_OPERATIONS,        \
      false, VELO_ENABLE_MY_TOOL, run_mytool)                                \
    ...
```

Fields: `id, slug, label, hint, group, danger, enable_flag, run_fn`.
`group` is `VELO_GROUP_OPERATIONS` (attack) or `VELO_GROUP_RECON`
(passive). `danger` adds the red styling. This single line is enough to make
the button appear — in the list order you write it.

### Step 2 — do the work (`components/actions/actions.c`)

Write the function you named. It runs in its own 4 KB task and must loop
until `*kill` is true, then clean up and return:

```c
static void run_mytool(volatile bool *kill) {
    ESP_LOGI(TAG, "my tool starting");
    my_tool_begin();
    while (!*kill) {
        my_tool_tick();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    my_tool_end();
}
```

That's it. Your tool now:

* shows up as a button in the console (grouped and ordered by the registry),
* is startable via `POST /api/action name=mytool`,
* is refused when disabled, and
* is scriptable with `action mytool ms=5000` — no parser edits.

Notes:
* Avoid large locals (4 KB stack); use file-scope buffers like the existing
  code does.
* Add a `uint32_t` to `action_counters_t` and bump it if you want a counter on
  the Recon page.
* If your tool needs settings, add a field to `velo_settings_t` and a setter
  (see `components/settings/`).

### Feature flags

Add `#define VELO_ENABLE_MY_TOOL 1` to `velobox_config.h` and pass it as the
`enable_flag` field. Set it to `0` to hide the tool from the UI and refuse to
start it — no other changes needed.

---

## 3. Changing the web UI

The console is plain HTML/CSS/JS in `data/` and is embedded into SPIFFS at
build time:

* `data/index.html` — structure (tabs, panels, tables).
* `data/style.css` — theme. The palette lives in `:root`; change `--accent`
  and the greys there to reskin the whole app.
* `data/app.js` — behaviour. It fetches `/api/capabilities` on load and
  renders the tool buttons from it, so you rarely need to edit it.

Rebuild the filesystem image after changing anything in `data/`:

```bash
pio run -e seeed_xiao_esp32c6 -t buildfs
```

---

## 4. Defaults and limits

* First-boot defaults: `VELO_DEFAULT_AP_SSID`, `VELO_DEFAULT_SLEEP_MS` in
  `velobox_config.h`, plus `settings_init_default()` in
  `components/settings/settings.c` (SSID list, timings, fake-AP defaults).
* Script limits: `SCRIPT_MAX_STEPS`, `SCRIPT_MAX_TEXT`, min/max durations in
  `components/script/script.h`.
* Fake-AP SSID capacity: `SETTINGS_MAX_SSIDS` in
  `components/settings/settings.h`.

Note: settings live in a single NVS blob, so changing the *size* of
`velo_settings_t` resets saved settings once on the next boot. That is
expected when you add a field.

---

## 5. Build and flash

```bash
# firmware + web assets
pio run -e seeed_xiao_esp32c6

# web assets only (after editing data/)
pio run -e seeed_xiao_esp32c6 -t buildfs

# flash firmware + filesystem
./flash.sh all
```

Host tests for the pure components (script, power, harvest, settings) run
without hardware:

```bash
pio test -e native
```

If a build fails with a missing component header after adding files, clear
the cached component graph once:

```bash
pio run -e seeed_xiao_esp32c6 -t clean
```

---

## 6. Where things live

```
include/velobox_config.h     build config: pins, branding, flags, defaults
components/actions/module_list.h   the module registry (add tools here)
components/actions/          execution engine + the run_* functions
components/script/           the script language (pure, host-tested)
components/settings/         NVS-backed settings + validation (pure)
components/power/            idle/sleep policy (pure)
components/harvest/          EAPOL/PMKID capture (pure)
components/recon/            monitor-mode scan
components/fakeap/           beacon/rogue AP
components/ble_spam/         BLE advertising floods
components/web/              HTTP server + /api/* endpoints
data/                        the web console (served from SPIFFS)
src/main.c                   hardware bring-up + ball state machine
```
