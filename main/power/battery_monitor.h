#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define BATTERY_LOW_THRESHOLD_PCT       20
#define BATTERY_CRITICAL_THRESHOLD_PCT   5

typedef enum {
    BATTERY_STATE_UNKNOWN,
    BATTERY_STATE_FULL,     // >90%
    BATTERY_STATE_GOOD,     // 50-90%
    BATTERY_STATE_NORMAL,   // 20-50%
    BATTERY_STATE_LOW,      // 10-20% — warn user
    BATTERY_STATE_CRITICAL, // <10%  — initiate sleep
} battery_state_t;

typedef struct {
    float           voltage_v;
    uint8_t         percent;
    battery_state_t state;
    bool            is_charging;
    uint32_t        estimated_minutes;
} battery_info_t;

esp_err_t    battery_monitor_init(void);
esp_err_t    battery_monitor_update(void);
esp_err_t    battery_monitor_get_info(battery_info_t *info);
uint8_t      battery_monitor_get_percent(void);
bool         battery_monitor_is_low(void);
bool         battery_monitor_is_critical(void);
const char  *battery_state_to_string(battery_state_t state);

#endif // BATTERY_MONITOR_H
