# PEBBLE — Product Thesis

## One line
> "A plantable, phone-native WiFi attack node that hands you cracked-ready WPA2 handshakes from your pocket."

## The gap nobody fills

Every WiFi tool today forces a compromise:

| Device | What it does well | Where it fails |
|---|---|---|
| **Flipper Zero + WiFi devboard** | Cute, accessible, apps ecosystem | SD offload, UART bottleneck, no plantable mode |
| **WiFi Pineapple Pager** | Purpose-built rogue AP, DuckyScript | $300+, desk-bound, phone UX secondary |
| **ESP32 Marauder (standalone)** | CLI captures, PMKID, cheap ($25) | OLED+buttons, serial expertise, no phone-native |
| **Pebble** (this project) | **Phone-native, plantable, stealth, one-tap, scriptable** | no evil-portal yet |

## The "impossible to refuse" demo

Three taps on your phone:
1. Recon fills the AP table (60 seconds)
2. Deauth a target → harvest captures the 4-way handshake automatically
3. Download the `.pcap` → hashcat breaks it offline

**Nobody else delivers that workflow in a $79 pocketable with no screen, no fiddling, and no SD card.**

## The user

- **Authorized red-team pentesters** (the owner's audience: "red team irl assessments")
- **Security trainers** teaching WPA2 at universities/CTFs
- **Bug bounty hunters** testing WiFi attack surfaces

## Why it wins

1. **Phone-native, not device-native** — no screen, no serial, no microSD. The phone is the UI. That's how Flipper won (mobile app + BLE), and we do it better for WiFi attacks.

2. **Plantable + stealth** — pocketable, boring AP name (configurable), no LEDs by default, ball-tilt kill switch. The Pineapple Pager is clipped to a belt with a 2.4" screen. Pebble disappears.

3. **"Grab and go" workflow** — one button = scan + capture + done. Marauder requires menu navigation and CLI knowledge. Our UI is 3 taps.

4. **Price gap** — ESP32-C6 BOM is ~$15. We sell for $79, half of Flipper+devboard ($234) and a quarter of the Pineapple Pager ($300).

5. **Open-source** — Flipper proved open + community > closed. Ship the firmware, share the roadmap.

## Roadmap

### V1 — "The Harvester"
- WiFi recon (AP + client tables, live counters)
- Real deauth (broadcast + targeted, `esp_wifi_80211_tx`)
- Probe flood, BLE scan, BLE spam
- **WPA handshake harvest** — 4-way EAPOL capture during any promiscuous session → `.pcap` download
- Configurable control-panel SSID, stealth by default
- 42 native tests, ESP-IDF 6 firmware, phone web UI

### V2 — "Complete the Kill Chain"
- **PMKID capture** — *passive detection done* (harvest extracts the PMKID KDE
  from any M1; displayed in the UI, cracks in hashcat `-m 22000`). *Active
  clientless trigger* (send EAPOL Start to the AP via `esp_wifi_80211_tx`,
  no client needed) pending on-hardware validation.
- **Evil portal** — fake AP with captive portal for credential harvesting
- **Capture persistence** — save `.pcap` files to SPIFFS/SPI flash, survive reboot
- **BLE remote control** — drive the device over BLE instead of WiFi for total stealth (phone talks BLE, device stays hidden)

### V3 — "The Pocket Red Team Kit"
- **Scriptable attacks** — *done*: a tiny per-line script language
  (`deauth`, `recon`, `blescan`, `blespam`, `probe`, `fakeap`, `wait` with
  `ap=`/`client=`/`ms=` args) edited in a new **Script** tab, persisted to
  SPIFFS `/spiffs/script.txt`, executed step-by-step by a dedicated task.
  Save, run, and stop from the phone.
- **Deep sleep + motion trigger** — *done*: after a configurable idle time
  (Settings → Power, default off) the box deep-sleeps; the ball tilt wakes it
  again. A deep sleep is a full reset, so it always wakes into safe mode. A
  30-minute timer keepalive prevents ever wedging asleep.
- **Excluded by product decision**: GPS wardriving, hardware badge, SD/JSON
  scripting. The target user is a plantable, phone-native pocket device, not
  a wardriving logger or a conference trinket.

### V4 — "Community Platform"
- Open-source public release with app store (SPIFFS-based plugin system)
- Partnership with pentest training orgs
- "Pebble Certified" hardware clones (ESP32-C5 for 5GHz, ESP32-S3 for dual-radio)

## Naming

- Codename: **Pebble** — drop it like a stone, watch traffic ripple
- SoftAP name: configurable (default "VeloBox", remains the operator's choice)
- Hardware: Seeed XIAO ESP32-C6 (tiny, cheap, WiFi 6 + BLE 5)
