#include "wifi_scanner.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "WIFI_SCAN";

static wifi_scan_result_t g_result   = {0};
static SemaphoreHandle_t  g_mutex    = NULL;
static bool               g_inited   = false;

esp_err_t wifi_scanner_init(void)
{
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) return ESP_ERR_NO_MEM;

    // WiFi may already be started by espnow_manager; guard with error check
    wifi_mode_t mode;
    esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret == ESP_ERR_WIFI_NOT_INIT) {
        esp_netif_init();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "init");
        ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    }

    g_result.best_rssi_dbm = -127;
    g_inited = true;
    ESP_LOGI(TAG, "WiFi scanner initialised");
    return ESP_OK;
}

esp_err_t wifi_scanner_deinit(void)
{
    g_inited = false;
    if (g_mutex) { vSemaphoreDelete(g_mutex); g_mutex = NULL; }
    return ESP_OK;
}

esp_err_t wifi_scanner_scan(void)
{
    if (!g_inited) return ESP_ERR_INVALID_STATE;

    wifi_scan_config_t scan_cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,     // all channels
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive = 100,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true); // blocking
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t ap_count = WIFI_SCANNER_MAX_APS;
    wifi_ap_record_t records[WIFI_SCANNER_MAX_APS];
    ret = esp_wifi_scan_get_ap_records(&ap_count, records);
    if (ret != ESP_OK) return ret;

    wifi_scan_result_t result = { .best_rssi_dbm = -127 };
    result.ap_count = (uint8_t)ap_count;

    for (uint16_t i = 0; i < ap_count; i++) {
        if (records[i].rssi > result.best_rssi_dbm) {
            result.best_rssi_dbm = records[i].rssi;
            result.best_channel  = records[i].primary;
            snprintf(result.best_ssid, sizeof(result.best_ssid),
                     "%s", (char *)records[i].ssid);
        }
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(&g_result, &result, sizeof(wifi_scan_result_t));
        xSemaphoreGive(g_mutex);
    }

    ESP_LOGI(TAG, "Scan: %d APs, best RSSI %d dBm (\"%s\")",
             ap_count, result.best_rssi_dbm, result.best_ssid);
    return ESP_OK;
}

void wifi_scanner_get_result(wifi_scan_result_t *out)
{
    if (!out || !g_mutex) return;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        memcpy(out, &g_result, sizeof(wifi_scan_result_t));
        xSemaphoreGive(g_mutex);
    }
}

void wifi_scanner_get_stats(protocol_stats_t *out)
{
    if (!out) return;
    wifi_scan_result_t r;
    wifi_scanner_get_result(&r);
    out->enabled       = g_inited;
    out->rssi_dbm      = r.best_rssi_dbm;
    out->packets_rx    = r.ap_count;   // repurpose: APs seen
    out->peer_in_range = (r.ap_count > 0);
}
