# VeloBox

> **Turn any phone into a WiFi attack console — from a $15 board.**
>
> No screen. No SD card. No laptop. No serial cable.
> Drop it, walk away, and pull cracked-ready WPA2 handshakes out of your pocket.

**VeloBox is a tiny ESP32-C6 node you plant, and a web console you open on your phone.** That's the whole product. It's open source, it runs on hardware you can buy for the price of lunch, and it does the one workflow that $300 of desk-bound gear still can't.

**If that's interesting: give it a star so other people find it too.**

---

## The pain

You already own a laptop, a big antenna, and hashcat. So why does WiFi assessment still feel like packing for a camping trip?

- **Flipper Zero + WiFi devboard** — expensive, and captures live on an SD card you offload by hand.
- **WiFi Pineapple Pager** — powerful, but clipped to your belt with a 2.4" screen. Great at a desk, a liability on a real assessment.
- **ESP32 Marauder** — cheap and genuinely cool, but it's an OLED and three buttons. If you don't know the menu tree, it does nothing for you.
- **Laptop + Alfa** — not plantable, and it screams "pentester" the second you open the bag.

Every option makes you choose: **cheap but annoying**, or **powerful but obvious**. None of them just... disappear into the room and let you run the op from your phone.

That's the gap VeloBox fills — and the code is yours to read, fork, and improve.

---

## The 3-tap demo

This is the entire point:

1. **Recon** — open `192.168.4.1` on your phone. The AP and client tables fill in live.
2. **Deauth** — tap a target. VeloBox sends real 802.11 deauth frames and **automatically harvests the 4-way handshake** while stations reconnect.
3. **Download** — save the `.pcap` straight from the browser and let hashcat `-m 22000` do the rest.

No SD card. No screen. No CLI. No laptop. Three taps, phone-native, from your pocket.

---

## What's actually in it

**Wireless attack & capture**
- **WiFi Deauth** — broadcast and targeted 802.11 disassociation via `esp_wifi_80211_tx`
- **WPA handshake harvest** — captures the EAPOL 4-way from any promiscuous session and exports a `.pcap`
- **PMKID extraction** — pulls the PMKID KDE from any M1, ready for hashcat `-m 22000`
- **Probe flood** — probe-request injection
- **Fake AP** — cloned-SSID beacon flood
- **WiFi recon** — monitor-mode AP + client discovery with live counters
- **BLE scan** — GAP device discovery
- **BLE spam** — advertising floods across profiles

**Operator features**
- **Phone-native console** — a slick dark web UI served from the device, zero app install
- **Scriptable attacks** — a tiny per-line language (`deauth`, `recon`, `blescan`, `blespam`, `probe`, `fakeap`, `wait`, and `action <anything>`) you edit on your phone, saved to flash, and run step-by-step
- **Plantable low-power mode** — it sleeps after a configurable idle timeout and wakes on the tilt switch. Pick it back up, it's live. A 30-minute keepalive means it never gets stuck asleep
- **Stealth by default** — boring, configurable AP name, no screen, no default lights
- **Haptics** — buzzer + vibration motor for quiet feedback

---

## Why this exists (and why it's free)

Every serious WiFi tool is either a closed box or a weekend project that only works if you already know how it works.

VeloBox is neither. It's a **working, phone-native red-team node** with a codebase small enough to actually understand — and built so the next person can extend it in an afternoon. I'm open-sourcing my first projects to earn traction the honest way: by shipping something people actually want to run.

**No price. No license gate on the idea. Just build it.**

If it's useful, the trade is simple: **star the repo, break it, file bugs, and send the modules you write back upstream.**

---

## Make it yours (the part nobody else gets right)

Most tools make you learn the whole codebase to change a button. VeloBox was designed so you don't have to.

**Want a different name, color, pins, or to disable a tool?**
Edit **one file**:

```c
#define VELO_BRAND_NAME "YourThing"
#define VELO_BRAND_TAGLINE "Whatever you want it to say"
#define VELO_ENABLE_DEAUTH 1
#define VELO_ENABLE_BLE_SPAM 1
```

**Want to add a whole new tool?**
It's **one line + one function.** The registry drives the UI, the API, the enable/disable logic, *and* the script engine — so your new tool automatically becomes a button **and** gets a script command. No HTML. No JavaScript. No parser edits.

```c
/* components/actions/module_list.h — add ONE line */
X(ACTION_MY_TOOL, "mytool", "My Tool", "One-line hint",
  VELO_GROUP_OPERATIONS, false, VELO_ENABLE_MY_TOOL, run_mytool)
```

```c
/* components/actions/actions.c — write the function */
static void run_mytool(volatile bool *kill) {
    while (!*kill) { my_tick(); vTaskDelay(pdMS_TO_TICKS(50)); }
}
```

That's it. Your tool now shows up in the console, works over the API, respects the feature flag, and scripts as `action mytool ms=5000`.

Full walkthrough: [`docs/customizing.md`](docs/customizing.md).

---

## How it's built (real parts, real wiring)

This isn't a devkit on a desk. Mine is a $15 board and a handful of parts I pulled out of a dead drone. You can build one for roughly the price of a takeout order.

**Bill of materials**

| Part | What it does | Notes |
|---|---|---|
| Seeed XIAO ESP32-C6 | The brain — WiFi 6 + BLE 5, antenna switch, LiPo pads | The one mandatory part |
| Tilt / motion switch | Detects a roll — arms the device *and* wakes it from sleep | Salvaged from a drone, or ~$1 |
| Vibration motor | Haptic feedback so you don't need lights or a screen | Pulled straight out of an old drone |
| NPN transistor + resistor | Drives the motor | A GPIO can't source motor current, so the motor runs through the transistor. Any small NPN (2N2222, S8050, ...) works |
| Piezo buzzer | Audible feedback / buzz counts | Cheap, optional but nice |
| Status LED + resistor | Blinks which mode you're in | Optional — stealth builds can leave it off |
| Single-cell LiPo | Makes it truly plantable | Plugs into the XIAO's battery pads |
| Tiny enclosure | Makes it disappear | Boring is the point |

**Wiring**

| GPIO | Goes to | How |
|---|---|---|
| `GPIO21` | Tilt / motion switch | Switch between the pin and GND. Firmware enables the internal pull-up and uses the roll edge as the wake source |
| `GPIO2` | Motor driver | Pin → base resistor → NPN base. Transistor collector → motor `-`, emitter → GND, motor `+` → 3V3/battery. Add a flyback diode across the motor |
| `GPIO10` | Piezo buzzer | Pin → buzzer `+`, buzzer `-` → GND |
| `GPIO15` | Status LED | Active **LOW** — LED cathode to the pin, anode to 3V3 through a resistor |
| `GPIO3` / `GPIO14` | Antenna switch | Already on the XIAO. The firmware enables the external antenna — remove this init and the radio goes deaf |

Every pin is defined in [`include/velobox_config.h`](include/velobox_config.h), so if you wire it differently, change one line.

**The stealth bit.** No screen, no default light show, a configurable AP name that reads like every other router in the building. The whole thing runs untethered off a LiPo, sleeps when it's left alone, and wakes the instant you pick it up.

---

## Build & flash it

**You need:** Seeed XIAO ESP32-C6, [PlatformIO](https://platformio.org/), USB-C cable.

```bash
git clone <this repo>
cd velo_box

pio run -e seeed_xiao_esp32c6        # build firmware + web assets
pio test -e native                    # 66 host tests, no hardware needed
./flash.sh all                        # flash firmware + filesystem
```

Then join the `VeloBox` WiFi network and open **http://192.168.4.1** on your phone.

Optional extras (all configurable in `include/velobox_config.h`): tilt switch, buzzer, vibration motor, external antenna switch.

---

## Under the hood

- **Hardware:** Seeed XIAO ESP32-C6 (WiFi 6, 2.4 GHz + BLE 5), external antenna switch, tilt/motion switch, piezo buzzer, and a transistor-driven vibration motor salvaged from an old drone
- **Firmware:** bare-metal **ESP-IDF** (no Arduino), NimBLE for BLE, `esp_wifi_80211_tx` for raw frame injection
- **Console:** plain HTML/CSS/JS, served from SPIFFS over the device's own softAP
- **Storage:** NVS for settings, SPIFFS for scripts and the web app
- **Quality:** a pure, host-tested core — `66/66` tests pass with no hardware attached
- **License:** [add your license here — MIT/Apache-2.0 recommended for adoption]

```
components/
  actions/    execution engine + the module registry (add tools here)
  script/     the scripting language (pure, host-tested)
  settings/   NVS-backed settings + validation (pure)
  power/      idle/sleep policy (pure)
  harvest/    EAPOL / PMKID capture (pure)
  recon/      monitor-mode scan
  fakeap/     beacon / rogue AP
  ble_spam/   BLE advertising floods
  web/        HTTP server + /api/* endpoints
data/         the web console (served from SPIFFS)
include/velobox_config.h   one-file build config
src/main.c    hardware bring-up + ball state machine
```

---

## Contribute (this is how it gets good)

This is a young project and the fastest way to shape it is to jump in.

- **Star it** — the single highest-leverage thing you can do for traction.
- **Write a module** — adding a tool is one line + one function. Send it as a PR and it ships to everyone.
- **File real bugs** — reproducible issues, logs, and steps. Vague reports get vague fixes.
- **Share a build** — fork it, reskin it, post it. Tag me and I'll boost it.

---

## FAQ

**Does it crack the password?**
No. It captures the handshake/PMKID and hands you a `.pcap`. You crack it offline with hashcat on your own GPU. That's the correct, reliable way to do it — and it's fast.

**Do I need a laptop?**
Not in the field. The whole op runs from the phone. You'll want a laptop later for hashcat.

**Is this legal?**
Only against networks you own or have written authorization to test. See below.

**Can I sell my own build?**
Yes — it's open source. Reskin it, extend it, ship it. Keeping a link back is appreciated, not required.

**Why give it away?**
Because the people who build on it will make it better than I ever could alone. The traction is the point.

---

## Status & roadmap

- **v3.2** — one-file config, one-line module registry, capabilities-driven UI, generic scripting
- **v3.1** — hardened plantable sleep, live script queue API, premium dark console
- **v3** — scriptable attack sequences + low-power sleep
- **v2.1 / v2** — WPA handshake harvest, PMKID extraction, `.pcap` export
- **v1** — recon, deauth, probe flood, BLE scan/spam, phone console

On the roadmap: evil-portal captive portal, active clientless PMKID, capture persistence, BLE remote control, and a community module store.

**Want to own a piece of the roadmap?** Open an issue and claim it.

---

## Legal / authorized use only

VeloBox is a professional security tool intended **strictly for authorized testing** of networks you own or have explicit written permission to assess. Deauthentication, rogue APs, and probe injection interfere with radio communications and may be illegal without authorization.

**You are responsible for how you use it.** The author assumes no liability for misuse or damage. Know your scope, get it in writing, stay in your lane.

---

Built for people who do the work — and do it quietly.
If you build something with it, send it back.
