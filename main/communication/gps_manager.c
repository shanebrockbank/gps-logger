/**
 * @file gps_manager.c
 * @brief GPS manager implementation
 */

#include "gps_manager.h"
#include "gps_driver.h"
#include "nmea_parser.h"
#include "system_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "GPS_MGR";

// GPS reading interval
#define GPS_READ_TIMEOUT_MS 1000
#define GPS_FIX_TIMEOUT_MS 30000  // 30 seconds without data = lost fix

// Private state
typedef struct {
    bool initialized;
    bool running;
    gps_state_t state;
    gps_data_t data;
    SemaphoreHandle_t mutex;
    TickType_t last_update_ticks;
} gps_manager_state_t;

static gps_manager_state_t gps_state = {
    .initialized = false,
    .running = false,
    .state = GPS_STATE_OFF,
    .mutex = NULL,
    .last_update_ticks = 0
};

/**
 * @brief Post GPS event to system manager
 */
static void gps_post_event(event_type_t event_type)
{
    system_manager_post_event(event_type, NULL);
}

/**
 * @brief Update GPS state and post events if state changed
 */
static void gps_update_state(gps_state_t new_state)
{
    if (gps_state.state == new_state) {
        return;  // No change
    }

    gps_state_t old_state = gps_state.state;
    gps_state.state = new_state;

    // Enhanced logging with readable state names
    const char *old_state_str = (old_state == GPS_STATE_OFF) ? "OFF" :
                                 (old_state == GPS_STATE_INITIALIZING) ? "INIT" :
                                 (old_state == GPS_STATE_NO_FIX) ? "NO_FIX" : "FIX_ACQUIRED";
    const char *new_state_str = (new_state == GPS_STATE_OFF) ? "OFF" :
                                 (new_state == GPS_STATE_INITIALIZING) ? "INIT" :
                                 (new_state == GPS_STATE_NO_FIX) ? "NO_FIX" : "FIX_ACQUIRED";
    ESP_LOGI(TAG, "State changed: %s -> %s", old_state_str, new_state_str);

    // Post events for state transitions
    if (new_state == GPS_STATE_FIX_ACQUIRED && old_state != GPS_STATE_FIX_ACQUIRED) {
        gps_post_event(EVENT_GPS_FIX_ACQUIRED);
    }
    else if (new_state != GPS_STATE_FIX_ACQUIRED && old_state == GPS_STATE_FIX_ACQUIRED) {
        gps_post_event(EVENT_GPS_FIX_LOST);
    }
}

esp_err_t gps_manager_init(void)
{
    if (gps_state.initialized) {
        ESP_LOGW(TAG, "GPS manager already initialized");
        return ESP_OK;
    }

    // Create mutex for thread-safe access to GPS data
    gps_state.mutex = xSemaphoreCreateMutex();
    if (gps_state.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    // Initialize GPS data structure
    memset(&gps_state.data, 0, sizeof(gps_data_t));

    // Initialize GPS driver
    esp_err_t err = gps_driver_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPS driver: %s", esp_err_to_name(err));
        vSemaphoreDelete(gps_state.mutex);
        return err;
    }

    gps_state.initialized = true;
    gps_state.state = GPS_STATE_INITIALIZING;
    ESP_LOGI(TAG, "GPS manager initialized");

    return ESP_OK;
}

esp_err_t gps_manager_deinit(void)
{
    if (!gps_state.initialized) {
        return ESP_OK;
    }

    gps_manager_stop();

    esp_err_t err = gps_driver_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize GPS driver: %s", esp_err_to_name(err));
    }

    if (gps_state.mutex != NULL) {
        vSemaphoreDelete(gps_state.mutex);
        gps_state.mutex = NULL;
    }

    gps_state.initialized = false;
    gps_state.state = GPS_STATE_OFF;
    ESP_LOGI(TAG, "GPS manager deinitialized");

    return ESP_OK;
}

esp_err_t gps_manager_start(void)
{
    if (!gps_state.initialized) {
        ESP_LOGE(TAG, "GPS manager not initialized");
        return ESP_FAIL;
    }

    if (gps_state.running) {
        ESP_LOGW(TAG, "GPS manager already running");
        return ESP_OK;
    }

    gps_state.running = true;
    gps_update_state(GPS_STATE_INITIALIZING);
    ESP_LOGI(TAG, "GPS manager started");

    return ESP_OK;
}

esp_err_t gps_manager_stop(void)
{
    if (!gps_state.running) {
        return ESP_OK;
    }

    gps_state.running = false;
    gps_update_state(GPS_STATE_OFF);
    ESP_LOGI(TAG, "GPS manager stopped");

    return ESP_OK;
}

void gps_manager_task(void *param)
{
    char nmea_buffer[GPS_MAX_NMEA_LENGTH];
    ESP_LOGI(TAG, "GPS task started");

    while (1) {
        // If not running, just wait
        if (!gps_state.running) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Read NMEA sentence from GPS
        esp_err_t err = gps_driver_read_sentence(nmea_buffer, sizeof(nmea_buffer), GPS_READ_TIMEOUT_MS);

        if (err == ESP_ERR_TIMEOUT) {
            // No data received - check if we should consider fix lost
            uint32_t time_since_update = gps_manager_get_time_since_update();
            if (time_since_update > GPS_FIX_TIMEOUT_MS) {
                if (xSemaphoreTake(gps_state.mutex, portMAX_DELAY) == pdTRUE) {
                    gps_update_state(GPS_STATE_NO_FIX);
                    xSemaphoreGive(gps_state.mutex);
                }
            }
            continue;
        }

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read GPS sentence: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Parse NMEA sentence
        gps_data_t new_data;
        if (xSemaphoreTake(gps_state.mutex, portMAX_DELAY) == pdTRUE) {
            // Copy current data as starting point (parsers update incrementally)
            memcpy(&new_data, &gps_state.data, sizeof(gps_data_t));

            // Parse the sentence
            if (nmea_parse_sentence(nmea_buffer, &new_data)) {
                // Update stored data
                memcpy(&gps_state.data, &new_data, sizeof(gps_data_t));
                gps_state.last_update_ticks = xTaskGetTickCount();

                // Update state based on fix status
                if (new_data.position_valid && new_data.fix_quality != GPS_FIX_INVALID) {
                    gps_update_state(GPS_STATE_FIX_ACQUIRED);
                } else {
                    gps_update_state(GPS_STATE_NO_FIX);
                }

                // Post data update event
                gps_post_event(EVENT_GPS_DATA_UPDATED);

                // Log position occasionally (every 10th fix)
                static uint32_t fix_count = 0;
                if (new_data.position_valid && (++fix_count % 10 == 0)) {
                    ESP_LOGI(TAG, "Position: %.6f, %.6f | Sats: %d | Alt: %.1fm",
                             new_data.latitude, new_data.longitude,
                             new_data.satellites, new_data.altitude);
                }
            }

            xSemaphoreGive(gps_state.mutex);
        }

        // Small delay to prevent task from hogging CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

gps_state_t gps_manager_get_state(void)
{
    return gps_state.state;
}

bool gps_manager_get_data(gps_data_t *data)
{
    if (!gps_state.initialized || data == NULL) {
        return false;
    }

    if (xSemaphoreTake(gps_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data, &gps_state.data, sizeof(gps_data_t));
        xSemaphoreGive(gps_state.mutex);
        return true;
    }

    return false;
}

bool gps_manager_has_fix(void)
{
    return gps_state.state == GPS_STATE_FIX_ACQUIRED;
}

uint32_t gps_manager_get_time_since_update(void)
{
    if (gps_state.last_update_ticks == 0) {
        return UINT32_MAX;  // Never updated
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed_ticks = now - gps_state.last_update_ticks;
    return pdTICKS_TO_MS(elapsed_ticks);
}
