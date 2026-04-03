#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include "protocol_types.h"
#include "esp_err.h"
#include <stdint.h>

typedef struct {
    uint16_t device_count;   // Devices seen in last scan window
    int8_t   best_rssi_dbm;  // Strongest device
} ble_scan_result_t;

esp_err_t ble_scanner_init(void);
esp_err_t ble_scanner_deinit(void);
esp_err_t ble_scanner_start(void);
esp_err_t ble_scanner_stop(void);
void      ble_scanner_get_result(ble_scan_result_t *out);
void      ble_scanner_get_stats(protocol_stats_t *out);

#endif // BLE_SCANNER_H
