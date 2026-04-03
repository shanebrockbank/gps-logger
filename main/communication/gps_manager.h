/**
 * @file gps_manager.h
 * @brief GPS manager - Orchestrates GPS operations
 *
 * This module follows the system manager pattern:
 * - It is a "dumb worker" that maintains GPS state
 * - Posts events to system manager when GPS state changes
 * - Does not make decisions (system manager decides what to do)
 * - Provides getter functions for current GPS data
 *
 * Architecture:
 * - GPS task continuously reads and parses NMEA sentences
 * - Updates internal GPS state structure
 * - Posts events to system manager on state changes:
 *   - GPS_EVENT_FIX_ACQUIRED: First valid fix obtained
 *   - GPS_EVENT_FIX_LOST: Fix lost
 *   - GPS_EVENT_DATA_UPDATED: New GPS data available
 *
 * Thread Safety:
 * - Single writer (GPS task)
 * - Multiple readers (via getter functions)
 * - Mutex protects GPS data structure
 */

#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include "nmea_parser.h"
#include "esp_err.h"
#include <stdbool.h>

/**
 * GPS manager state
 */
typedef enum {
    GPS_STATE_OFF,          // GPS powered off
    GPS_STATE_INITIALIZING, // GPS initializing (waiting for first fix)
    GPS_STATE_NO_FIX,       // GPS on but no position fix
    GPS_STATE_FIX_ACQUIRED  // GPS has valid position fix
} gps_state_t;

/**
 * @brief Initialize GPS manager
 *
 * Initializes GPS driver and prepares for operation.
 * Must be called before gps_manager_start().
 *
 * @return ESP_OK on success
 * @return ESP_FAIL on failure
 */
esp_err_t gps_manager_init(void);

/**
 * @brief Deinitialize GPS manager
 *
 * Stops GPS manager and frees resources.
 *
 * @return ESP_OK on success
 */
esp_err_t gps_manager_deinit(void);

/**
 * @brief Start GPS manager
 *
 * Powers on GPS and begins reading NMEA sentences.
 * Call this when entering ACTIVE state.
 *
 * @return ESP_OK on success
 */
esp_err_t gps_manager_start(void);

/**
 * @brief Stop GPS manager
 *
 * Stops reading GPS data (but doesn't deinitialize).
 * Call this when entering low power states.
 *
 * @return ESP_OK on success
 */
esp_err_t gps_manager_stop(void);

/**
 * @brief GPS task function
 *
 * Continuously reads NMEA sentences from GPS and updates state.
 * This should be run as a FreeRTOS task.
 *
 * @param param Unused task parameter
 */
void gps_manager_task(void *param);

/**
 * @brief Get current GPS state
 *
 * @return Current GPS state
 */
gps_state_t gps_manager_get_state(void);

/**
 * @brief Get current GPS data (thread-safe)
 *
 * Copies current GPS data to provided structure.
 *
 * @param data Pointer to structure to receive GPS data
 * @return true if data is valid and copied successfully
 * @return false if GPS not initialized or no data available
 */
bool gps_manager_get_data(gps_data_t *data);

/**
 * @brief Check if GPS has a valid fix
 *
 * @return true if GPS has a valid position fix
 * @return false if no fix or GPS not initialized
 */
bool gps_manager_has_fix(void);

/**
 * @brief Get time since last GPS update
 *
 * @return Milliseconds since last successful GPS data update
 */
uint32_t gps_manager_get_time_since_update(void);

#endif // GPS_MANAGER_H
