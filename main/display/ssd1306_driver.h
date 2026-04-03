#ifndef SSD1306_DRIVER_H
#define SSD1306_DRIVER_H

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

// SSD1306 display dimensions
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

// Display power states
typedef enum {
    SSD1306_POWER_OFF,     // Display completely off
    SSD1306_POWER_DIM,     // Display on with low brightness
    SSD1306_POWER_FULL,    // Display on with full brightness
} ssd1306_power_t;

// SSD1306 device handle
typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t address;
    uint8_t buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];  // Frame buffer (1 bit per pixel)
    ssd1306_power_t power_state;
} ssd1306_handle_t;

// Initialization and power control
esp_err_t ssd1306_init(ssd1306_handle_t *handle, uint8_t i2c_address);
esp_err_t ssd1306_set_power(ssd1306_handle_t *handle, ssd1306_power_t power);
esp_err_t ssd1306_free(ssd1306_handle_t *handle);

// Display update
esp_err_t ssd1306_update(ssd1306_handle_t *handle);  // Send buffer to display

// Drawing primitives
esp_err_t ssd1306_clear(ssd1306_handle_t *handle);
esp_err_t ssd1306_set_pixel(ssd1306_handle_t *handle, uint8_t x, uint8_t y, bool on);
esp_err_t ssd1306_draw_char(ssd1306_handle_t *handle, uint8_t x, uint8_t y, char c);
esp_err_t ssd1306_draw_string(ssd1306_handle_t *handle, uint8_t x, uint8_t y, const char *str);
esp_err_t ssd1306_draw_line(ssd1306_handle_t *handle, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on);

#endif // SSD1306_DRIVER_H
