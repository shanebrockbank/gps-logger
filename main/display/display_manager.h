#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "esp_err.h"
#include "ssd1306_driver.h"

// Display identifiers
typedef enum {
    DISPLAY_1,  // Primary display (GPS data) at 0x3C
    DISPLAY_2,  // Status display (power/metrics) at 0x3D
    DISPLAY_COUNT
} display_id_t;

// Display brightness levels
typedef enum {
    DISPLAY_BRIGHTNESS_OFF,   // Display off
    DISPLAY_BRIGHTNESS_DIM,   // Dim brightness
    DISPLAY_BRIGHTNESS_FULL,  // Full brightness
} display_brightness_t;

// Screen types for single-display cycling
typedef enum {
    SCREEN_GPS,      // GPS screen (position, fix status, speed)
    SCREEN_STATUS,   // Status screen (power, uptime, battery)
    SCREEN_RANGING,  // Ranging screen (protocol stats, distance)
    SCREEN_COUNT
} screen_type_t;

// Display manager functions
esp_err_t display_manager_init(void);
esp_err_t display_manager_set_brightness(display_id_t display, display_brightness_t brightness);
esp_err_t display_manager_set_all_brightness(display_brightness_t brightness);
esp_err_t display_manager_cycle_brightness(void);  // OFF → DIM → FULL → OFF

// Get display handle for direct rendering
ssd1306_handle_t* display_manager_get_display(display_id_t display);

// Update displays (send buffer to hardware)
esp_err_t display_manager_update(display_id_t display);
esp_err_t display_manager_update_all(void);

// Get current brightness level
display_brightness_t display_manager_get_brightness(void);

// Screen cycling (for single-display mode)
esp_err_t display_manager_cycle_screen(void);  // Cycle: GPS → STATUS → GPS
screen_type_t display_manager_get_active_screen(void);

#endif // DISPLAY_MANAGER_H
