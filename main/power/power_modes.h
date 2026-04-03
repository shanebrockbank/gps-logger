#ifndef POWER_MODES_H
#define POWER_MODES_H

#include "esp_err.h"
#include <stdbool.h>

// Power mode definitions
typedef enum {
    POWER_MODE_ACTIVE,      // 240MHz, no sleep
    POWER_MODE_PERFORMANCE, // 160MHz, auto light sleep
    POWER_MODE_BALANCED,    // 80MHz, auto light sleep
    POWER_MODE_LOW_POWER,   // 40MHz, auto light sleep
    POWER_MODE_COUNT        // Total number of power modes (for cycling)
} power_mode_t;

// Power mode management
esp_err_t power_modes_init(void);
esp_err_t power_modes_set_mode(power_mode_t mode);
power_mode_t power_modes_get_current(void);
const char* power_modes_get_name(power_mode_t mode);

// CPU frequency control
esp_err_t power_modes_set_cpu_freq(uint32_t freq_mhz);
uint32_t power_modes_get_cpu_freq(void);

// Sleep control
esp_err_t power_modes_enable_light_sleep(bool enable);
esp_err_t power_modes_enter_deep_sleep(uint64_t sleep_time_us, int wake_gpio);

#endif // POWER_MODES_H
