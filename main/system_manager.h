#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

// System states
typedef enum {
    SYS_STATE_BOOT,          // Initial boot, hardware init
    SYS_STATE_LIGHT_SLEEP,   // Light sleep (standby), quick wake
    SYS_STATE_ACTIVE,        // Fully active, all peripherals on
    SYS_STATE_IDLE,          // Idle, reduced activity
    SYS_STATE_RANGING,       // Ranging: all wireless + SD logging active
} system_state_t;

// Event types
typedef enum {
    EVENT_BUTTON_1_PRESS,       // Button 1 (GPIO32) - Cycle screens
    EVENT_BUTTON_2_PRESS,       // Button 2 (GPIO33) - Cycle power modes
    EVENT_BUTTON_3_PRESS,       // Button 3 (GPIO27) - Return to light sleep
    EVENT_GPS_FIX_ACQUIRED,     // GPS has acquired position fix
    EVENT_GPS_FIX_LOST,         // GPS has lost position fix
    EVENT_GPS_DATA_UPDATED,     // New GPS data available
    EVENT_RANGING_START,        // Begin multi-protocol ranging mode
    EVENT_RANGING_STOP,         // End ranging mode
    EVENT_LOW_BATTERY,          // Battery below low threshold
    EVENT_CRITICAL_BATTERY,     // Battery below critical — sleep now
} event_type_t;

// Event structure
typedef struct {
    event_type_t type;
    void *data;
    uint32_t timestamp;
} system_event_t;

// System manager functions
esp_err_t system_manager_init(void);
void system_manager_task(void *pvParameters);
esp_err_t system_manager_post_event(event_type_t type, void *data);
system_state_t system_manager_get_state(void);
const char* system_state_to_string(system_state_t state);

// INA226 monitoring task
void ina226_monitor_task(void *pvParameters);

// Global event queue handle (accessed by modules)
extern QueueHandle_t g_system_event_queue;

#endif // SYSTEM_MANAGER_H
