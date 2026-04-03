#include "screen_status.h"
#include "esp_log.h"
#include <stdio.h>

// Logging tag - immutable, no thread safety needed
static const char *TAG = "SCR_STATUS";

// Status screen layout constants (Y coordinates)
#define STATUS_SCREEN_TITLE_Y       0   // Title and screen indicator
#define STATUS_SCREEN_INDICATOR_X   102 // X position for screen indicator
#define STATUS_SCREEN_CURRENT_Y     16  // Current line (moved to avoid color boundary)
#define STATUS_SCREEN_VOLTAGE_Y     24  // Voltage line
#define STATUS_SCREEN_POWER_Y       32  // Power line
#define STATUS_SCREEN_UPTIME_Y      44  // Uptime line
#define STATUS_SCREEN_PHASE_Y       54  // Phase indicator line

// Buffer sizes
#define STATUS_SCREEN_BUF_SIZE      25

esp_err_t screen_status_render(ssd1306_handle_t *display, const power_metrics_t *metrics)
{
    if (display == NULL || metrics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Clear display buffer
    ssd1306_clear(display);

    // Line 1: Title + Screen indicator
    ssd1306_draw_string(display, 0, STATUS_SCREEN_TITLE_Y, "POWER STATUS");
    ssd1306_draw_string(display, STATUS_SCREEN_INDICATOR_X, STATUS_SCREEN_TITLE_Y, "[2/2]");

    // Line 2: Current (mA)
    char current_str[STATUS_SCREEN_BUF_SIZE];
    snprintf(current_str, sizeof(current_str), "I: %.1f mA", metrics->current_ma);
    ssd1306_draw_string(display, 0, STATUS_SCREEN_CURRENT_Y, current_str);

    // Line 3: Voltage (V)
    char voltage_str[STATUS_SCREEN_BUF_SIZE];
    snprintf(voltage_str, sizeof(voltage_str), "V: %.2f V", metrics->voltage_v);
    ssd1306_draw_string(display, 0, STATUS_SCREEN_VOLTAGE_Y, voltage_str);

    // Line 4: Power (mW)
    char power_str[STATUS_SCREEN_BUF_SIZE];
    snprintf(power_str, sizeof(power_str), "P: %.1f mW", metrics->power_mw);
    ssd1306_draw_string(display, 0, STATUS_SCREEN_POWER_Y, power_str);

    // Line 5: Uptime
    uint32_t hours = metrics->uptime_sec / 3600;
    uint32_t minutes = (metrics->uptime_sec % 3600) / 60;
    uint32_t seconds = metrics->uptime_sec % 60;

    char uptime_str[STATUS_SCREEN_BUF_SIZE];
    snprintf(uptime_str, sizeof(uptime_str), "Up: %02luh%02lum%02lus",
             (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
    ssd1306_draw_string(display, 0, STATUS_SCREEN_UPTIME_Y, uptime_str);

    // Line 6: Phase indicator
    ssd1306_draw_string(display, 0, STATUS_SCREEN_PHASE_Y, "Phase 3");

    return ESP_OK;
}
