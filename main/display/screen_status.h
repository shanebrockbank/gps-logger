#ifndef SCREEN_STATUS_H
#define SCREEN_STATUS_H

#include "esp_err.h"
#include "ssd1306_driver.h"

// Power metrics structure
typedef struct {
    float current_ma;
    float voltage_v;
    float power_mw;
    uint32_t uptime_sec;
} power_metrics_t;

// Status screen rendering
esp_err_t screen_status_render(ssd1306_handle_t *display, const power_metrics_t *metrics);

#endif // SCREEN_STATUS_H
