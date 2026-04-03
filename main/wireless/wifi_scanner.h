#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include "protocol_types.h"
#include "esp_err.h"
#include <stdint.h>

/** Maximum APs recorded per scan */
#define WIFI_SCANNER_MAX_APS  20

typedef struct {
    uint8_t  ap_count;       // APs found in last scan
    int8_t   best_rssi_dbm;  // Strongest AP signal
    uint8_t  best_channel;   // Channel of strongest AP
    char     best_ssid[33];  // SSID of strongest AP
} wifi_scan_result_t;

esp_err_t wifi_scanner_init(void);
esp_err_t wifi_scanner_deinit(void);

/** Trigger a synchronous scan (blocks ~300 ms). */
esp_err_t wifi_scanner_scan(void);

/** Get result of last scan (thread-safe). */
void wifi_scanner_get_result(wifi_scan_result_t *out);

/** Fill protocol_stats_t with WiFi scan data. */
void wifi_scanner_get_stats(protocol_stats_t *out);

#endif // WIFI_SCANNER_H
