#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "system_manager.h"
#include "power/power_manager.h"
#include "power/power_modes.h"
#include "power/ina226_driver.h"
#include "power/battery_monitor.h"
#include "input/button_handler.h"
#include "display/display_manager.h"
#include "display/screen_gps.h"
#include "display/screen_status.h"
#include "display/screen_ranging.h"
#include "communication/gps_manager.h"
#include "wireless/espnow_manager.h"
#include "wireless/wifi_scanner.h"
#include "wireless/ble_scanner.h"
#include "wireless/lora_manager.h"
#include "storage/sd_logger.h"
#include "gps/distance_calc.h"

static const char *TAG = "MAIN";

// Task priorities (higher = more urgent)
#define TASK_PRIORITY_SYSTEM_MGR   10
#define TASK_PRIORITY_BUTTON        8
#define TASK_PRIORITY_GPS           7
#define TASK_PRIORITY_RANGING       6
#define TASK_PRIORITY_DISPLAY       5
#define TASK_PRIORITY_MONITOR       4

// Task stack sizes (bytes)
#define TASK_STACK_SYSTEM_MGR    4096
#define TASK_STACK_BUTTON        2048
#define TASK_STACK_GPS           3072
#define TASK_STACK_RANGING       4096
#define TASK_STACK_DISPLAY       4096
#define TASK_STACK_MONITOR       3072

#define DISPLAY_UPDATE_RATE_MS    500   // 2 Hz
#define RANGING_TX_INTERVAL_MS   1000  // broadcast position every 1 s
#define BATTERY_UPDATE_INTERVAL_MS 10000

// Shared protocol stats — written by ranging task, read by display task
// Thread safety: ranging task is sole writer; display reads are best-effort
static protocol_stats_t g_proto_stats[PROTOCOL_COUNT] = {0};
static float            g_distance_m = -1.0f;

// ── Helper: gather power metrics ──────────────────────────────────────────────

static void gather_power_metrics(power_metrics_t *m, uint64_t t0)
{
    if (!m) return;
    if (ina226_read_current(&m->current_ma) != ESP_OK) m->current_ma = 0.0f;
    if (ina226_read_voltage(&m->voltage_v)  != ESP_OK) m->voltage_v  = 0.0f;
    if (ina226_read_power(&m->power_mw)     != ESP_OK) m->power_mw   = 0.0f;
    m->uptime_sec = (uint32_t)((esp_timer_get_time() - t0) / 1000000ULL);
}

// ── Display update task ───────────────────────────────────────────────────────

void display_update_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Display task started");
    vTaskDelay(pdMS_TO_TICKS(2000));

    uint64_t t0 = esp_timer_get_time();

    while (1) {
        screen_type_t screen = display_manager_get_active_screen();
        ssd1306_handle_t *disp = display_manager_get_display(DISPLAY_1);

        if (disp == NULL) {
            vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_RATE_MS));
            continue;
        }

        system_state_t state = system_manager_get_state();

        switch (screen) {
            case SCREEN_GPS:
                screen_gps_render(disp, state, 0);
                break;

            case SCREEN_STATUS: {
                power_metrics_t m = {0};
                gather_power_metrics(&m, t0);
                screen_status_render(disp, &m);
                break;
            }

            case SCREEN_RANGING:
                screen_ranging_render(disp, g_proto_stats,
                                      g_distance_m, sd_logger_is_active());
                break;

            default:
                break;
        }

        display_manager_update(DISPLAY_1);
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_RATE_MS));
    }
}

// ── Ranging task ──────────────────────────────────────────────────────────────

void ranging_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Ranging task started");

    // Start all wireless protocols
    espnow_manager_start();
    ble_scanner_start();
    lora_manager_start();

    // Start SD session
    if (sd_logger_start_session() == ESP_OK) {
        ESP_LOGI(TAG, "SD logging started");
    }

    uint32_t last_wifi_scan = 0;
    uint32_t last_battery   = 0;

    while (system_manager_get_state() == SYS_STATE_RANGING) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Transmit our GPS position via ESP-NOW and LoRa every 1 s
        gps_data_t gps = {0};
        bool has_fix = gps_manager_get_data(&gps) && gps.position_valid;

        if (has_fix) {
            uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000ULL);
            espnow_manager_broadcast_position(gps.latitude, gps.longitude,
                                              (int32_t)(gps.altitude * 100.0), ts);
            lora_manager_send_position(gps.latitude, gps.longitude,
                                       (int32_t)(gps.altitude * 100.0), ts);
        }

        // Poll LoRa RX
        position_packet_t lora_pkt = {0};
        int8_t lora_rssi = 0;
        lora_manager_receive(&lora_pkt, &lora_rssi);

        // Periodic WiFi scan (~every 10 s)
        if (now - last_wifi_scan > 10000) {
            wifi_scanner_scan();
            last_wifi_scan = now;
        }

        // Update protocol stats snapshots
        espnow_manager_get_stats(&g_proto_stats[PROTOCOL_ESPNOW]);
        wifi_scanner_get_stats(&g_proto_stats[PROTOCOL_WIFI]);
        ble_scanner_get_stats(&g_proto_stats[PROTOCOL_BLE]);
        lora_manager_get_stats(&g_proto_stats[PROTOCOL_LORA]);

        // Calculate best distance from any in-range peer
        g_distance_m = -1.0f;
        if (has_fix) {
            for (int i = 0; i < PROTOCOL_COUNT; i++) {
                if (g_proto_stats[i].peer_in_range) {
                    position_packet_t *p = &g_proto_stats[i].peer_pos;
                    float d = distance_calc_haversine(gps.latitude, gps.longitude,
                                                      p->latitude, p->longitude);
                    if (d >= 0.0f && (g_distance_m < 0.0f || d < g_distance_m)) {
                        g_distance_m = d;
                    }
                    g_proto_stats[i].distance_m = d;
                }
            }
        }

        // Battery check
        if (now - last_battery > BATTERY_UPDATE_INTERVAL_MS) {
            battery_monitor_update();
            last_battery = now;
            if (battery_monitor_is_critical()) {
                system_manager_post_event(EVENT_CRITICAL_BATTERY, NULL);
            } else if (battery_monitor_is_low()) {
                system_manager_post_event(EVENT_LOW_BATTERY, NULL);
            }
        }

        // Log to SD card
        if (sd_logger_is_active() && has_fix) {
            float current_ma = 0.0f;
            float voltage_v  = 0.0f;
            ina226_read_current(&current_ma);
            ina226_read_voltage(&voltage_v);
            sd_logger_write_row(gps.latitude, gps.longitude,
                                voltage_v, current_ma,
                                g_proto_stats, g_distance_m);
        }

        vTaskDelay(pdMS_TO_TICKS(RANGING_TX_INTERVAL_MS));
    }

    // Cleanup when ranging ends
    espnow_manager_stop();
    ble_scanner_stop();
    lora_manager_stop();
    sd_logger_end_session();

    ESP_LOGI(TAG, "Ranging task complete");
    vTaskDelete(NULL);
}

// ── app_main ──────────────────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "=== GPS Logger Starting ===");
    ESP_LOGI(TAG, "ESP-IDF %s", esp_get_idf_version());

    // NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Core subsystems (order matters)
    ESP_ERROR_CHECK(power_modes_init());
    ESP_ERROR_CHECK(button_handler_init());
    ESP_ERROR_CHECK(power_manager_init());       // initialises I2C
    ESP_ERROR_CHECK(system_manager_init());
    ESP_ERROR_CHECK(display_manager_init());

    // Battery monitor (depends on INA226 being ready)
    ESP_ERROR_CHECK(battery_monitor_init());

    // GPS
    ESP_ERROR_CHECK(gps_manager_init());
    ESP_ERROR_CHECK(gps_manager_start());

    // Wireless (non-fatal — hardware may not be fitted)
    if (espnow_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "ESP-NOW init failed — skipped");
    }
    if (wifi_scanner_init() != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scanner init failed — skipped");
    }
    if (ble_scanner_init() != ESP_OK) {
        ESP_LOGW(TAG, "BLE scanner init failed — skipped");
    }
    if (lora_manager_init(LORA_FREQ_868MHZ) != ESP_OK) {
        ESP_LOGW(TAG, "LoRa init failed — skipped (module fitted?)");
    }

    // SD card (non-fatal)
    if (sd_logger_init() != ESP_OK) {
        ESP_LOGW(TAG, "SD card not found — logging disabled");
    }

    // FreeRTOS tasks
    xTaskCreatePinnedToCore(system_manager_task, "sys_mgr",
        TASK_STACK_SYSTEM_MGR, NULL, TASK_PRIORITY_SYSTEM_MGR, NULL, 0);

    xTaskCreatePinnedToCore(button_handler_task, "btn_hdlr",
        TASK_STACK_BUTTON, NULL, TASK_PRIORITY_BUTTON, NULL, 0);

    xTaskCreatePinnedToCore(gps_manager_task, "gps",
        TASK_STACK_GPS, NULL, TASK_PRIORITY_GPS, NULL, 1);

    xTaskCreatePinnedToCore(display_update_task, "disp_upd",
        TASK_STACK_DISPLAY, NULL, TASK_PRIORITY_DISPLAY, NULL, 1);

    xTaskCreatePinnedToCore(ina226_monitor_task, "ina226_mon",
        TASK_STACK_MONITOR, NULL, TASK_PRIORITY_MONITOR, NULL, 1);

    ESP_LOGI(TAG, "All tasks created — system running");
}
