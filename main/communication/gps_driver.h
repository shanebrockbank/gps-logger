/**
 * @file gps_driver.h
 * @brief Low-level GPS UART driver for NEO-M8N module
 *
 * This module provides UART communication with the GPS module and
 * reads raw NMEA sentences. It is a "dumb worker" - it doesn't parse
 * NMEA data or make decisions, just provides clean UART interface.
 *
 * Hardware: NEO-M8N connected via UART2
 * - RX: GPIO16
 * - TX: GPIO17
 * - Baud: 9600 8N1
 *
 * Thread Safety:
 * - Single writer (GPS task)
 * - UART driver handles internal locking
 */

#ifndef GPS_DRIVER_H
#define GPS_DRIVER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * Maximum length of an NMEA sentence (including \r\n)
 * NMEA 0183 standard allows up to 82 characters
 */
#define GPS_MAX_NMEA_LENGTH 100

/**
 * @brief Initialize GPS UART driver
 *
 * Configures UART2 for GPS communication at 9600 baud, 8N1.
 * Installs UART driver with RX buffer.
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if UART initialization fails
 */
esp_err_t gps_driver_init(void);

/**
 * @brief Deinitialize GPS UART driver
 *
 * Uninstalls UART driver and frees resources.
 *
 * @return ESP_OK on success
 */
esp_err_t gps_driver_deinit(void);

/**
 * @brief Read one complete NMEA sentence from GPS
 *
 * Blocks until a complete NMEA sentence is received (terminated by \r\n)
 * or timeout occurs. The sentence is null-terminated and newline characters
 * are removed.
 *
 * @param buffer Buffer to store NMEA sentence
 * @param buffer_size Size of buffer in bytes
 * @param timeout_ms Timeout in milliseconds
 *
 * @return ESP_OK if sentence received successfully
 * @return ESP_ERR_TIMEOUT if timeout occurred
 * @return ESP_ERR_INVALID_SIZE if sentence too long for buffer
 * @return ESP_FAIL on other errors
 */
esp_err_t gps_driver_read_sentence(char *buffer, size_t buffer_size, uint32_t timeout_ms);

/**
 * @brief Check if GPS UART has data available
 *
 * @return true if data is available to read
 * @return false if no data available
 */
bool gps_driver_data_available(void);

/**
 * @brief Flush GPS UART RX buffer
 *
 * Discards all unread data in the receive buffer.
 *
 * @return ESP_OK on success
 */
esp_err_t gps_driver_flush(void);

#endif // GPS_DRIVER_H
