/**
 * @file gps_driver.c
 * @brief GPS UART driver implementation
 */

#include "gps_driver.h"
#include "pin_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "GPS_DRIVER";

// UART configuration
#define GPS_UART_NUM UART_NUM_2
#define GPS_BAUD_RATE 9600
#define GPS_RX_BUFFER_SIZE 1024
#define GPS_TX_BUFFER_SIZE 0  // We don't send commands to GPS (yet)

// NMEA sentence markers
#define NMEA_START_CHAR '$'
#define NMEA_CR '\r'
#define NMEA_LF '\n'

// Private state
static bool initialized = false;

esp_err_t gps_driver_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "GPS driver already initialized");
        return ESP_OK;
    }

    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = GPS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(GPS_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        return err;
    }

    // Set UART pins
    err = uart_set_pin(GPS_UART_NUM, GPS_UART_TX_GPIO, GPS_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return err;
    }

    // Install UART driver
    err = uart_driver_install(GPS_UART_NUM, GPS_RX_BUFFER_SIZE, GPS_TX_BUFFER_SIZE,
                              0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return err;
    }

    initialized = true;
    ESP_LOGI(TAG, "GPS driver initialized (UART%d, %d baud, RX=%d, TX=%d)",
             GPS_UART_NUM, GPS_BAUD_RATE, GPS_UART_RX_GPIO, GPS_UART_TX_GPIO);

    return ESP_OK;
}

esp_err_t gps_driver_deinit(void)
{
    if (!initialized) {
        return ESP_OK;
    }

    esp_err_t err = uart_driver_delete(GPS_UART_NUM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete UART driver: %s", esp_err_to_name(err));
        return err;
    }

    initialized = false;
    ESP_LOGI(TAG, "GPS driver deinitialized");

    return ESP_OK;
}

esp_err_t gps_driver_read_sentence(char *buffer, size_t buffer_size, uint32_t timeout_ms)
{
    if (!initialized) {
        ESP_LOGE(TAG, "GPS driver not initialized");
        return ESP_FAIL;
    }

    if (buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid buffer");
        return ESP_ERR_INVALID_ARG;
    }

    size_t index = 0;
    bool sentence_started = false;
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    // Read characters until we get a complete sentence or timeout
    while (1) {
        // Check for timeout
        if ((xTaskGetTickCount() - start_ticks) >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }

        // Read one byte at a time
        uint8_t byte;
        int len = uart_read_bytes(GPS_UART_NUM, &byte, 1, pdMS_TO_TICKS(100));

        if (len <= 0) {
            continue;  // No data yet, keep waiting
        }

        // Look for sentence start
        if (!sentence_started) {
            if (byte == NMEA_START_CHAR) {
                sentence_started = true;
                buffer[index++] = byte;
            }
            continue;
        }

        // We're in a sentence, accumulate characters
        if (index >= buffer_size - 1) {
            ESP_LOGE(TAG, "NMEA sentence too long for buffer");
            return ESP_ERR_INVALID_SIZE;
        }

        buffer[index++] = byte;

        // Check for sentence end (LF follows CR)
        if (byte == NMEA_LF && index >= 2 && buffer[index - 2] == NMEA_CR) {
            // Remove \r\n and null-terminate
            index -= 2;
            buffer[index] = '\0';
            return ESP_OK;
        }
    }
}

bool gps_driver_data_available(void)
{
    if (!initialized) {
        return false;
    }

    size_t available = 0;
    uart_get_buffered_data_len(GPS_UART_NUM, &available);
    return available > 0;
}

esp_err_t gps_driver_flush(void)
{
    if (!initialized) {
        return ESP_FAIL;
    }

    return uart_flush(GPS_UART_NUM);
}
