#pragma once

#include "recon_core.h"
#include <stdbool.h>
#include <stddef.h>

/* WiFi sniffer driver. Switches the radio to promiscuous RX while the
   softAP keeps beaconing (radio is shared: channels hop, so the phone
   may flake briefly during a scan). All table access must happen with
   recon_lock()/recon_unlock() held. */

void recon_init(void);

/* Clear tables. Safe anytime; takes the lock internally. */
void recon_reset(void);

/* Start sniffing: enable promiscuous RX + reset counters. Returns
   false if the radio isn't in a usable state. */
bool recon_start(void);

/* Stop sniffing and return the AP channel to 1. */
void recon_stop(void);

bool recon_running(void);

/* Sweep all channels once (dwell ~60 ms). Call this repeatedly from the
   scan task loop for as long as you want to keep scanning. */
void recon_hop_once(void);

void recon_lock(void);
void recon_unlock(void);

/* Helpers that already hold the lock and build host-agnostic data.
   Snapshot a target BSSID's AP row if present. */
bool recon_get_ap(const uint8_t bssid[6], recon_ap_t *out);