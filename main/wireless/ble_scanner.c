#include "ble_scanner.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "BLE_SCAN";

static ble_scan_result_t g_result  = { .best_rssi_dbm = -127 };
static SemaphoreHandle_t g_mutex   = NULL;
static bool              g_inited  = false;

// BLE scan parameters — passive, 100 ms window every 200 ms
static const esp_ble_scan_params_t SCAN_PARAMS = {
    .scan_type          = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x0140,   // 200 ms
    .scan_window        = 0x00A0,   // 100 ms
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    if (event != ESP_GAP_BLE_SCAN_RESULT_EVT) return;
    if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;

    int8_t rssi = param->scan_rst.rssi;

    if (xSemaphoreTake(g_mutex, 0) == pdTRUE) {
        g_result.device_count++;
        if (rssi > g_result.best_rssi_dbm) g_result.best_rssi_dbm = rssi;
        xSemaphoreGive(g_mutex);
    }
}

esp_err_t ble_scanner_init(void)
{
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) return ESP_ERR_NO_MEM;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_cfg), TAG, "bt ctrl init");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_BLE), TAG, "bt enable");
    ESP_RETURN_ON_ERROR(esp_bluedroid_init(), TAG, "bluedroid init");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "bluedroid enable");
    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_event_handler), TAG, "gap cb");

    g_inited = true;
    ESP_LOGI(TAG, "BLE scanner initialised");
    return ESP_OK;
}

esp_err_t ble_scanner_deinit(void)
{
    ble_scanner_stop();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    g_inited = false;
    if (g_mutex) { vSemaphoreDelete(g_mutex); g_mutex = NULL; }
    return ESP_OK;
}

esp_err_t ble_scanner_start(void)
{
    if (!g_inited) return ESP_ERR_INVALID_STATE;

    // Reset counters for fresh scan window
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_result.device_count  = 0;
        g_result.best_rssi_dbm = -127;
        xSemaphoreGive(g_mutex);
    }

    ESP_RETURN_ON_ERROR(esp_ble_gap_set_scan_params(
        (esp_ble_scan_params_t *)&SCAN_PARAMS), TAG, "set params");
    ESP_RETURN_ON_ERROR(esp_ble_gap_start_scanning(0 /* continuous */), TAG, "start");
    ESP_LOGI(TAG, "BLE scan started");
    return ESP_OK;
}

esp_err_t ble_scanner_stop(void)
{
    if (!g_inited) return ESP_OK;
    esp_ble_gap_stop_scanning();
    ESP_LOGI(TAG, "BLE scan stopped (%d devices)", g_result.device_count);
    return ESP_OK;
}

void ble_scanner_get_result(ble_scan_result_t *out)
{
    if (!out || !g_mutex) return;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        memcpy(out, &g_result, sizeof(ble_scan_result_t));
        xSemaphoreGive(g_mutex);
    }
}

void ble_scanner_get_stats(protocol_stats_t *out)
{
    if (!out) return;
    ble_scan_result_t r;
    ble_scanner_get_result(&r);
    out->enabled       = g_inited;
    out->rssi_dbm      = r.best_rssi_dbm;
    out->packets_rx    = r.device_count;
    out->peer_in_range = (r.device_count > 0);
}
