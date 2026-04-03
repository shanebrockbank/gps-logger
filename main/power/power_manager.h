#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "esp_err.h"

// Power management initialization
esp_err_t power_manager_init(void);

// Future power control functions will be added here:
// - GPS power control (enable/disable GPS module)
// - Display power control (brightness/sleep)
// - Wireless module power control
// - SD card power management

#endif // POWER_MANAGER_H
