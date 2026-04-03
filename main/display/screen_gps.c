#include "screen_gps.h"
#include "communication/gps_manager.h"
#include <stdio.h>

// GPS screen layout constants (Y coordinates)
#define GPS_SCREEN_TITLE_Y        0   // Title and screen indicator
#define GPS_SCREEN_INDICATOR_X    102 // X position for screen indicator
#define GPS_SCREEN_STATE_Y        16  // System state line (moved to avoid color boundary)
#define GPS_SCREEN_LAT_Y          24  // Latitude line
#define GPS_SCREEN_LON_Y          32  // Longitude line
#define GPS_SCREEN_BUTTON_Y       44  // Button counter line
#define GPS_SCREEN_STATUS_Y       54  // Status message line

// Buffer sizes
#define GPS_SCREEN_STATE_BUF_SIZE  25
#define GPS_SCREEN_BUTTON_BUF_SIZE 25

esp_err_t screen_gps_render(ssd1306_handle_t *display, system_state_t sys_state, uint32_t button_count)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Clear display buffer
    ssd1306_clear(display);

    // Line 1: Title + Screen indicator
    ssd1306_draw_string(display, 0, GPS_SCREEN_TITLE_Y, "GPS LOGGER");
    ssd1306_draw_string(display, GPS_SCREEN_INDICATOR_X, GPS_SCREEN_TITLE_Y, "[1/2]");

    // Get GPS data
    gps_data_t gps_data;
    bool has_gps_data = gps_manager_get_data(&gps_data);
    gps_state_t gps_state = gps_manager_get_state();

    // Line 2: GPS status
    char status_str[GPS_SCREEN_STATE_BUF_SIZE];
    if (gps_state == GPS_STATE_OFF) {
        snprintf(status_str, sizeof(status_str), "GPS: OFF");
    } else if (!has_gps_data || !gps_data.position_valid) {
        // Show satellite count if available
        if (has_gps_data && gps_data.satellites > 0) {
            snprintf(status_str, sizeof(status_str), "GPS: Sats %d", gps_data.satellites);
        } else {
            snprintf(status_str, sizeof(status_str), "GPS: No Fix");
        }
    } else {
        snprintf(status_str, sizeof(status_str), "GPS: Fix %d Sats", gps_data.satellites);
    }
    ssd1306_draw_string(display, 0, GPS_SCREEN_STATE_Y, status_str);

    // Line 3-4: GPS coordinates or waiting message
    char lat_str[25];
    char lon_str[25];

    if (has_gps_data && gps_data.position_valid) {
        // Format latitude with direction
        char lat_dir = (gps_data.latitude >= 0) ? 'N' : 'S';
        double lat_abs = (gps_data.latitude >= 0) ? gps_data.latitude : -gps_data.latitude;
        snprintf(lat_str, sizeof(lat_str), "%.6f %c", lat_abs, lat_dir);

        // Format longitude with direction
        char lon_dir = (gps_data.longitude >= 0) ? 'E' : 'W';
        double lon_abs = (gps_data.longitude >= 0) ? gps_data.longitude : -gps_data.longitude;
        snprintf(lon_str, sizeof(lon_str), "%.6f %c", lon_abs, lon_dir);
    } else {
        snprintf(lat_str, sizeof(lat_str), "Waiting for fix...");
        lon_str[0] = '\0';  // Empty string
    }

    ssd1306_draw_string(display, 0, GPS_SCREEN_LAT_Y, lat_str);
    ssd1306_draw_string(display, 0, GPS_SCREEN_LON_Y, lon_str);

    // Line 5: HDOP (if fix available)
    char hdop_str[GPS_SCREEN_BUTTON_BUF_SIZE];
    if (has_gps_data && gps_data.position_valid) {
        snprintf(hdop_str, sizeof(hdop_str), "HDOP: %.2f", gps_data.hdop);
    } else {
        hdop_str[0] = '\0';  // Empty string when no fix
    }
    ssd1306_draw_string(display, 0, GPS_SCREEN_BUTTON_Y, hdop_str);

    // Line 6: Speed and heading (if fix available)
    char speed_hdg_str[25];
    if (has_gps_data && gps_data.position_valid) {
        // Convert speed from knots to km/h (1 knot = 1.852 km/h)
        float speed_kph = gps_data.speed_knots * 1.852f;
        snprintf(speed_hdg_str, sizeof(speed_hdg_str), "%.1fkph (%03.0f°)", speed_kph, gps_data.course);
    } else {
        speed_hdg_str[0] = '\0';  // Empty string when no fix
    }
    ssd1306_draw_string(display, 0, GPS_SCREEN_STATUS_Y, speed_hdg_str);

    return ESP_OK;
}
