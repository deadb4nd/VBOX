#ifndef BLE_BEACON_H
#define BLE_BEACON_H

/**
 * @brief Initialize and start the BLE beacon.
 *
 * Initializes NVS flash, the NimBLE host stack, GAP service (if enabled),
 * and host configuration, then starts the NimBLE host task.
 */
void init_ble_beacon(void);

#endif
