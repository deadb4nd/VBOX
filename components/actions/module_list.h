#pragma once

#include "velobox_config.h"

/*
 * ================== THE MODULE REGISTRY ==================
 *
 * This is the only place you list tools. Adding a tool means two things:
 *
 *   1. add ONE line below (X(...) macro)
 *   2. write its run_* function in components/actions/actions.c
 *
 * Everything else is generated from this table: the ACTION_* enum, the
 * web console buttons, the /api/action endpoint, the list order, and the
 * compile-time enable/disable check. Nothing to edit in HTML or JS.
 *
 *   X(id, slug, label, hint, group, danger, enable_flag, run_fn)
 *
 *   id          unique ACTION_* enum name
 *   slug        stable id used by the API / UI / scripts (lowercase, no spaces)
 *   label       button text
 *   hint        one-line description shown under the button
 *   group       VELO_GROUP_OPERATIONS (attack) or VELO_GROUP_RECON (passive)
 *   danger      true = destructive, gets danger styling
 *   enable_flag VELO_ENABLE_* macro from velobox_config.h (0 = hidden + refused)
 *   run_fn      the function you write; runs until *kill becomes true
 *
 * After editing, rebuild. To add the same module to scripts too, see
 * docs/customizing.md (that part is not automatic yet).
 */

#define VELO_MODULE_LIST(X)                                                  \
    X(ACTION_WIFI_DEAUTH, "deauth", "WiFi Deauth",                           \
      "802.11 disassociation flood", VELO_GROUP_OPERATIONS, true,            \
      VELO_ENABLE_DEAUTH, run_deauth)                                        \
    X(ACTION_BLE_SPAM, "ble", "BLE Spam", "Advertising flood, all profiles", \
      VELO_GROUP_OPERATIONS, false, VELO_ENABLE_BLE_SPAM, run_ble_spam)      \
    X(ACTION_FAKE_AP, "fakeap", "Fake AP",                                   \
      "Beacon flood with cloned SSIDs", VELO_GROUP_OPERATIONS, false,        \
      VELO_ENABLE_FAKE_AP, run_fakeap)                                       \
    X(ACTION_RECON, "recon", "WiFi Scan", "AP + client discovery",           \
      VELO_GROUP_RECON, false, VELO_ENABLE_RECON, run_recon)                 \
    X(ACTION_BLE_SCAN, "blescan", "BLE Scan", "GAP device discovery",        \
      VELO_GROUP_RECON, false, VELO_ENABLE_BLE_SCAN, run_blescan)            \
    X(ACTION_PROBE_FLOOD, "probe", "Probe Flood", "Probe-request injection", \
      VELO_GROUP_RECON, false, VELO_ENABLE_PROBE, run_probe)
