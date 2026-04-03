#include "system_manager.h"
#include "power/power_manager.h"
#include "power/power_modes.h"
#include "power/ina226_driver.h"
#include "power/battery_monitor.h"
#include "display/display_manager.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/task.h"

// Logging tag - immutable, no thread safety needed
static const char *TAG = "SYS_MGR";

// Configuration constants
#define SYSTEM_EVENT_QUEUE_SIZE  10   // Max pending events in queue
#define EVENT_POST_TIMEOUT_MS    100  // Timeout for posting events
#define INA226_BOOT_DELAY_MS     1000 // Wait for system stabilization
#define INA226_SAMPLE_RATE_MS    2000 // INA226 reading interval

// System event queue - shared between all tasks posting events
// Thread safety: FreeRTOS queue is thread-safe, safe for multi-task access
// Owned by: system_manager module
QueueHandle_t g_system_event_queue = NULL;

// Current system state - owned by system_manager task
// Thread safety: Only modified by system_manager_task, read-only for others (atomic reads OK)
static system_state_t current_state = SYS_STATE_BOOT;

// State transition handlers
static system_state_t handle_boot_state(system_event_t *event);
static system_state_t handle_light_sleep_state(system_event_t *event);
static system_state_t handle_active_state(system_event_t *event);
static system_state_t handle_idle_state(system_event_t *event);
static system_state_t handle_ranging_state(system_event_t *event);

// INA226 monitoring task
void ina226_monitor_task(void *pvParameters);

esp_err_t system_manager_init(void)
{
    // Create event queue
    g_system_event_queue = xQueueCreate(SYSTEM_EVENT_QUEUE_SIZE, sizeof(system_event_t));
    if (g_system_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "System manager initialized");
    return ESP_OK;
}

esp_err_t system_manager_post_event(event_type_t type, void *data)
{
    if (g_system_event_queue == NULL) {
        ESP_LOGE(TAG, "Event queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    system_event_t event = {
        .type = type,
        .data = data,
        .timestamp = xTaskGetTickCount()
    };

    if (xQueueSend(g_system_event_queue, &event, pdMS_TO_TICKS(EVENT_POST_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to post event %d", type);
        return ESP_FAIL;
    }

    return ESP_OK;
}

system_state_t system_manager_get_state(void)
{
    return current_state;
}

const char* system_state_to_string(system_state_t state)
{
    switch (state) {
        case SYS_STATE_BOOT:        return "BOOT";
        case SYS_STATE_LIGHT_SLEEP: return "LIGHT_SLEEP";
        case SYS_STATE_ACTIVE:      return "ACTIVE";
        case SYS_STATE_IDLE:        return "IDLE";
        case SYS_STATE_RANGING:     return "RANGING";
        default:                    return "UNKNOWN";
    }
}

// Dispatch event to appropriate state handler
static system_state_t dispatch_event_to_handler(system_event_t *event)
{
    switch (current_state) {
        case SYS_STATE_BOOT:
            return handle_boot_state(event);

        case SYS_STATE_LIGHT_SLEEP:
            return handle_light_sleep_state(event);

        case SYS_STATE_ACTIVE:
            return handle_active_state(event);

        case SYS_STATE_IDLE:
            return handle_idle_state(event);

        case SYS_STATE_RANGING:
            return handle_ranging_state(event);

        default:
            ESP_LOGW(TAG, "Unknown state: %d", current_state);
            return current_state;  // Stay in current state on error
    }
}

void system_manager_task(void *pvParameters)
{
    system_event_t event;

    ESP_LOGI(TAG, "System manager task started");

    // Transition from BOOT to LIGHT_SLEEP immediately
    current_state = SYS_STATE_LIGHT_SLEEP;
    ESP_LOGI(TAG, "System ready: LIGHT_SLEEP (standby)");

    while (1) {
        // Wait for events (block indefinitely until event arrives)
        if (!xQueueReceive(g_system_event_queue, &event, portMAX_DELAY)) {
            // Should never happen with portMAX_DELAY
            continue;
        }

        // Event received - process it
        ESP_LOGD(TAG, "Event received: type=%d, state=%s",
                 event.type, system_state_to_string(current_state));

        system_state_t next_state = dispatch_event_to_handler(&event);

        // Handle state transition if needed
        if (next_state != current_state) {
            ESP_LOGI(TAG, "State transition: %s -> %s",
                     system_state_to_string(current_state),
                     system_state_to_string(next_state));
            current_state = next_state;
        }
    }
}

// State handlers

static system_state_t handle_boot_state(system_event_t *event)
{
    // Boot always transitions to light sleep (standby mode)
    ESP_LOGI(TAG, "Boot complete, entering light sleep (standby)");
    return SYS_STATE_LIGHT_SLEEP;
}

static system_state_t handle_light_sleep_state(system_event_t *event)
{
    if (event->type == EVENT_BUTTON_1_PRESS) {
        // Cycle between screens (GPS ↔ STATUS)
        display_manager_cycle_screen();
        ESP_LOGI(TAG, "Button 1: Screen cycled");
        return SYS_STATE_LIGHT_SLEEP;
    }

    if (event->type == EVENT_BUTTON_2_PRESS) {
        // Cycle through power modes
        power_mode_t current = power_modes_get_current();
        power_mode_t next = (power_mode_t)((current + 1) % POWER_MODE_COUNT);
        power_modes_set_mode(next);
        ESP_LOGI(TAG, "Button 2: Power mode changed to %s", power_modes_get_name(next));
        return SYS_STATE_LIGHT_SLEEP;
    }

    if (event->type == EVENT_BUTTON_3_PRESS) {
        ESP_LOGI(TAG, "Button 3: Transitioning to ACTIVE");
        return SYS_STATE_ACTIVE;
    }

    if (event->type == EVENT_GPS_FIX_ACQUIRED) {
        ESP_LOGI(TAG, "GPS fix acquired");
        return SYS_STATE_LIGHT_SLEEP;
    }

    if (event->type == EVENT_GPS_FIX_LOST) {
        ESP_LOGI(TAG, "GPS fix lost");
        return SYS_STATE_LIGHT_SLEEP;
    }

    if (event->type == EVENT_GPS_DATA_UPDATED) {
        // GPS data updated - display will fetch new data on next refresh
        return SYS_STATE_LIGHT_SLEEP;
    }

    return SYS_STATE_LIGHT_SLEEP;
}

static system_state_t handle_active_state(system_event_t *event)
{
    if (event->type == EVENT_BUTTON_1_PRESS) {
        // Cycle between screens (GPS ↔ STATUS)
        display_manager_cycle_screen();
        ESP_LOGI(TAG, "Button 1: Screen cycled");
        return SYS_STATE_ACTIVE;
    }

    if (event->type == EVENT_BUTTON_2_PRESS) {
        // Cycle through power modes
        power_mode_t current = power_modes_get_current();
        power_mode_t next = (power_mode_t)((current + 1) % POWER_MODE_COUNT);
        power_modes_set_mode(next);
        ESP_LOGI(TAG, "Button 2: Power mode changed to %s", power_modes_get_name(next));
        return SYS_STATE_ACTIVE;
    }

    if (event->type == EVENT_BUTTON_3_PRESS) {
        ESP_LOGI(TAG, "Button 3: Returning to light sleep (standby)");
        return SYS_STATE_LIGHT_SLEEP;
    }

    if (event->type == EVENT_RANGING_START) {
        ESP_LOGI(TAG, "Entering RANGING mode");
        return SYS_STATE_RANGING;
    }

    if (event->type == EVENT_GPS_FIX_ACQUIRED) return SYS_STATE_ACTIVE;
    if (event->type == EVENT_GPS_FIX_LOST)     return SYS_STATE_ACTIVE;
    if (event->type == EVENT_GPS_DATA_UPDATED)  return SYS_STATE_ACTIVE;

    if (event->type == EVENT_LOW_BATTERY) {
        ESP_LOGW(TAG, "Low battery — switching to LOW_POWER mode");
        power_modes_set_mode(POWER_MODE_LOW_POWER);
        return SYS_STATE_ACTIVE;
    }

    if (event->type == EVENT_CRITICAL_BATTERY) {
        ESP_LOGE(TAG, "Critical battery — entering deep sleep");
        power_modes_enter_deep_sleep(0, 0);
        return SYS_STATE_LIGHT_SLEEP; // not reached
    }

    return SYS_STATE_ACTIVE;
}

static system_state_t handle_ranging_state(system_event_t *event)
{
    if (event->type == EVENT_BUTTON_1_PRESS) {
        display_manager_cycle_screen();
        return SYS_STATE_RANGING;
    }

    if (event->type == EVENT_BUTTON_2_PRESS) {
        ESP_LOGI(TAG, "Button 2: Stopping ranging");
        return SYS_STATE_ACTIVE;
    }

    if (event->type == EVENT_BUTTON_3_PRESS) {
        ESP_LOGI(TAG, "Button 3: Ranging → light sleep");
        return SYS_STATE_LIGHT_SLEEP;
    }

    if (event->type == EVENT_RANGING_STOP) {
        ESP_LOGI(TAG, "Ranging stopped");
        return SYS_STATE_ACTIVE;
    }

    if (event->type == EVENT_CRITICAL_BATTERY) {
        ESP_LOGE(TAG, "Critical battery during ranging — sleeping");
        power_modes_enter_deep_sleep(0, 0);
        return SYS_STATE_LIGHT_SLEEP;
    }

    if (event->type == EVENT_GPS_DATA_UPDATED) return SYS_STATE_RANGING;
    if (event->type == EVENT_GPS_FIX_ACQUIRED) return SYS_STATE_RANGING;
    if (event->type == EVENT_GPS_FIX_LOST)     return SYS_STATE_RANGING;

    return SYS_STATE_RANGING;
}

static system_state_t handle_idle_state(system_event_t *event)
{
    if (event->type == EVENT_BUTTON_1_PRESS) {
        // Cycle between screens (GPS ↔ STATUS)
        display_manager_cycle_screen();
        ESP_LOGI(TAG, "Button 1: Screen cycled");
        return SYS_STATE_IDLE;
    }

    if (event->type == EVENT_BUTTON_2_PRESS) {
        // Cycle through power modes
        power_mode_t current = power_modes_get_current();
        power_mode_t next = (power_mode_t)((current + 1) % POWER_MODE_COUNT);
        power_modes_set_mode(next);
        ESP_LOGI(TAG, "Button 2: Power mode changed to %s", power_modes_get_name(next));
        return SYS_STATE_IDLE;
    }

    if (event->type == EVENT_BUTTON_3_PRESS) {
        ESP_LOGI(TAG, "Button 3: Transitioning to light sleep");
        return SYS_STATE_LIGHT_SLEEP;
    }

    if (event->type == EVENT_GPS_FIX_ACQUIRED) {
        ESP_LOGI(TAG, "GPS fix acquired");
        return SYS_STATE_IDLE;
    }

    if (event->type == EVENT_GPS_FIX_LOST) {
        ESP_LOGI(TAG, "GPS fix lost");
        return SYS_STATE_IDLE;
    }

    if (event->type == EVENT_GPS_DATA_UPDATED) {
        // GPS data updated - display will fetch new data on next refresh
        return SYS_STATE_IDLE;
    }

    return SYS_STATE_IDLE;
}

// ========================================================================
// INA226 Monitoring Task
// ========================================================================

void ina226_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "INA226 monitor task started");

    // Wait for system to stabilize after boot
    vTaskDelay(pdMS_TO_TICKS(INA226_BOOT_DELAY_MS));

    while (1) {
        float current_ma = 0.0f;
        float voltage_v = 0.0f;
        float power_mw = 0.0f;

        // Read INA226 values
        esp_err_t ret = ina226_read_current(&current_ma);
        if (ret == ESP_OK) {
            ina226_read_voltage(&voltage_v);
            ina226_read_power(&power_mw);

            // Log with current power mode
            ESP_LOGI(TAG, "[%s] Current: %.1f mA | Voltage: %.2f V | Power: %.1f mW",
                     power_modes_get_name(power_modes_get_current()),
                     current_ma, voltage_v, power_mw);
        }

        // Sample at configured rate
        vTaskDelay(pdMS_TO_TICKS(INA226_SAMPLE_RATE_MS));
    }
}
