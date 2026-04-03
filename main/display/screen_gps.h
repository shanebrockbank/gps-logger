#ifndef SCREEN_GPS_H
#define SCREEN_GPS_H

#include "esp_err.h"
#include "ssd1306_driver.h"
#include "system_manager.h"

// GPS screen rendering
esp_err_t screen_gps_render(ssd1306_handle_t *display, system_state_t sys_state, uint32_t button_count);

#endif // SCREEN_GPS_H
