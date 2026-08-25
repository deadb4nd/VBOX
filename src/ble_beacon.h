#ifndef BLE_BEACON_H
#define BLE_BEACON_H

/**
 * @brief Initialize and start the BLE beacon.
 *
 * Initializes NVS flash, the NimBLE host stack, GAP service (if enabled),
 * and host configuration, then starts the NimBLE host task.
 */
void init_ble_beacon(void);

/**
 * @brief Stop the BLE beacon and power down the radio.
 *
 * Stops the NimBLE port and deinitializes it, cleanly shutting down
 * the BLE stack.
 */
void stop_ble_beacon(void);

#endif // BLE_BEACON_H
