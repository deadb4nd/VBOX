#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BLE_SCAN_MAX_DEVICES 24
#define BLE_SCAN_NAME_LEN 33

void ble_spam_init(void);
void ble_spam_run_once(void);
void ble_spam_kitchen_sink_run_once(void); /* NEW */
void ble_spam_stop(void);

/* BLE scanner (NimBLE GAP discovery), coexists with the spamming
   controller: discovery is only run when the BLE-scan action asks for
   it, after any active advertising has been stopped. */
void ble_scan_start(void);  /* stops adv, then scans continuously */
void ble_scan_stop(void);   /* stops discovery (if any) */
bool ble_scan_running(void);
void ble_scan_reset(void);
uint32_t ble_scan_count(void);
void ble_scan_get(uint32_t i, uint8_t mac[6], char name[BLE_SCAN_NAME_LEN],
                  int8_t *rssi);
