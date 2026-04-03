#include "sd_logger.h"
#include "pin_config.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG      = "SD_LOG";
static const char *MOUNT_PT = "/sdcard";

#define CSV_HEADER \
    "timestamp,lat,lon,battery_v,current_ma," \
    "espnow_rssi,espnow_in_range," \
    "wifi_rssi,wifi_in_range," \
    "ble_rssi,ble_in_range," \
    "lora_rssi,lora_in_range," \
    "distance_m\n"

#define FLUSH_INTERVAL_ROWS  10   // Flush every N rows (~10 s at 1 Hz)

static sdmmc_card_t *g_card    = NULL;
static FILE         *g_file    = NULL;
static bool          g_active  = false;
static uint32_t      g_row_cnt = 0;
static uint32_t      g_session = 0;

// ── Mount / unmount ───────────────────────────────────────────────────────────

esp_err_t sd_logger_init(void)
{
    // SPI bus may already be initialised by LoRa; use add_device only
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs   = SD_CARD_CS_GPIO;
    slot.host_id   = (spi_host_device_t)host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(MOUNT_PT, &host, &slot,
                                             &mount_cfg, &g_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, g_card);
    ESP_LOGI(TAG, "SD card mounted at %s", MOUNT_PT);
    return ESP_OK;
}

esp_err_t sd_logger_deinit(void)
{
    sd_logger_end_session();
    if (g_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT_PT, g_card);
        g_card = NULL;
    }
    return ESP_OK;
}

// ── Session management ────────────────────────────────────────────────────────

esp_err_t sd_logger_start_session(void)
{
    if (!g_card) return ESP_ERR_INVALID_STATE;
    if (g_active) sd_logger_end_session();

    g_session++;
    char path[64];
    snprintf(path, sizeof(path), "%s/session_%04lu.csv", MOUNT_PT, (unsigned long)g_session);

    g_file = fopen(path, "w");
    if (!g_file) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return ESP_FAIL;
    }

    fputs(CSV_HEADER, g_file);
    fflush(g_file);

    g_active  = true;
    g_row_cnt = 0;
    ESP_LOGI(TAG, "Session %lu started: %s", (unsigned long)g_session, path);
    return ESP_OK;
}

esp_err_t sd_logger_end_session(void)
{
    if (!g_active || !g_file) return ESP_OK;
    fflush(g_file);
    fclose(g_file);
    g_file   = NULL;
    g_active = false;
    ESP_LOGI(TAG, "Session %lu ended (%lu rows)", (unsigned long)g_session, (unsigned long)g_row_cnt);
    return ESP_OK;
}

// ── Row writing ───────────────────────────────────────────────────────────────

esp_err_t sd_logger_write_row(double lat, double lon,
                              float battery_v, float current_ma,
                              const protocol_stats_t stats[],
                              float distance_m)
{
    if (!g_active || !g_file) return ESP_ERR_INVALID_STATE;

    uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    // Defensive: stats array must have PROTOCOL_COUNT entries
    const protocol_stats_t *en = &stats[PROTOCOL_ESPNOW];
    const protocol_stats_t *wi = &stats[PROTOCOL_WIFI];
    const protocol_stats_t *bl = &stats[PROTOCOL_BLE];
    const protocol_stats_t *lo = &stats[PROTOCOL_LORA];

    int n = fprintf(g_file,
        "%lu,%.6f,%.6f,%.3f,%.1f,"
        "%d,%d,"
        "%d,%d,"
        "%d,%d,"
        "%d,%d,"
        "%.1f\n",
        (unsigned long)ts, lat, lon, battery_v, current_ma,
        en->rssi_dbm, en->peer_in_range ? 1 : 0,
        wi->rssi_dbm, wi->peer_in_range ? 1 : 0,
        bl->rssi_dbm, bl->peer_in_range ? 1 : 0,
        lo->rssi_dbm, lo->peer_in_range ? 1 : 0,
        distance_m);

    if (n < 0) return ESP_FAIL;

    g_row_cnt++;

    if (g_row_cnt % FLUSH_INTERVAL_ROWS == 0) {
        fflush(g_file);
    }

    return ESP_OK;
}

esp_err_t sd_logger_flush(void)
{
    if (!g_active || !g_file) return ESP_OK;
    fflush(g_file);
    return ESP_OK;
}

bool sd_logger_is_active(void) { return g_active; }
