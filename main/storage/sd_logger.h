#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include "wireless/protocol_types.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t sd_logger_init(void);
esp_err_t sd_logger_deinit(void);
esp_err_t sd_logger_start_session(void);
esp_err_t sd_logger_end_session(void);

/**
 * @brief Write one CSV row.
 *
 * @param lat        Our latitude (decimal deg)
 * @param lon        Our longitude (decimal deg)
 * @param battery_v  Battery voltage (V)
 * @param current_ma System current draw (mA)
 * @param stats      Array of PROTOCOL_COUNT protocol_stats_t entries
 * @param distance_m Distance to peer (m), or -1 if unknown
 */
esp_err_t sd_logger_write_row(double lat, double lon,
                              float battery_v, float current_ma,
                              const protocol_stats_t stats[],
                              float distance_m);

esp_err_t sd_logger_flush(void);

bool sd_logger_is_active(void);

#endif // SD_LOGGER_H
