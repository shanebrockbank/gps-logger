#include "display_manager.h"
#include "communication/i2c_bus.h"
#include "pin_config.h"
#include "esp_log.h"

// Logging tag - immutable, no thread safety needed
static const char *TAG = "DISP_MGR";

// Configuration constants
#define I2C_SCAN_MAX_DEVICES  10  // Maximum devices to detect during initialization

// Display manager state - owned by display_manager module
// Thread safety: Not thread-safe, should only be called from display task
// Used by: display_update_task in main.c
static ssd1306_handle_t displays[DISPLAY_COUNT];
static bool display_initialized[DISPLAY_COUNT] = {false, false};
static display_brightness_t current_brightness = DISPLAY_BRIGHTNESS_FULL;

// Screen cycling state (for single-display mode)
// Thread safety: Only modified by button events via system_manager, atomic reads OK
static screen_type_t active_screen = SCREEN_GPS;

// Display I2C addresses - immutable configuration
static const uint8_t display_addresses[DISPLAY_COUNT] = {
    DISPLAY_1_I2C_ADDR,  // 0x3C
    DISPLAY_2_I2C_ADDR,  // 0x3D
};

esp_err_t display_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing display manager...");

    // Scan I2C bus for displays
    uint8_t device_count = 0;
    uint8_t addresses[I2C_SCAN_MAX_DEVICES];
    esp_err_t ret = i2c_bus_scan(&device_count, addresses, I2C_SCAN_MAX_DEVICES);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to scan I2C bus");
        return ret;
    }

    // Initialize each display if detected
    for (int i = 0; i < DISPLAY_COUNT; i++) {
        bool found = false;

        // Check if display address was found in scan
        for (int j = 0; j < device_count; j++) {
            if (addresses[j] == display_addresses[i]) {
                found = true;
                break;
            }
        }

        if (found) {
            ESP_LOGI(TAG, "Initializing Display %d at address 0x%02X...", i + 1, display_addresses[i]);

            ret = ssd1306_init(&displays[i], display_addresses[i]);
            if (ret == ESP_OK) {
                display_initialized[i] = true;
                ESP_LOGI(TAG, "Display %d initialized successfully", i + 1);

                // Clear display and show "READY"
                ssd1306_clear(&displays[i]);
                char msg[20];
                snprintf(msg, sizeof(msg), "Display %d", i + 1);
                ssd1306_draw_string(&displays[i], 0, 0, msg);
                ssd1306_draw_string(&displays[i], 0, 10, "READY");
                ssd1306_update(&displays[i]);
            } else {
                ESP_LOGW(TAG, "Failed to initialize Display %d", i + 1);
            }
        } else {
            ESP_LOGW(TAG, "Display %d not found at address 0x%02X", i + 1, display_addresses[i]);
        }
    }

    // Check if at least one display was initialized
    if (!display_initialized[DISPLAY_1] && !display_initialized[DISPLAY_2]) {
        ESP_LOGW(TAG, "No displays initialized - continuing without displays");
        // Don't fail - displays are optional for development
        ESP_LOGI(TAG, "Display manager initialized (0 display(s) active)");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Display manager initialized (%d display(s) active)",
             display_initialized[DISPLAY_1] + display_initialized[DISPLAY_2]);

    return ESP_OK;
}

esp_err_t display_manager_set_brightness(display_id_t display, display_brightness_t brightness)
{
    if (display >= DISPLAY_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!display_initialized[display]) {
        ESP_LOGW(TAG, "Display %d not initialized", display + 1);
        return ESP_ERR_INVALID_STATE;
    }

    ssd1306_power_t power_state;
    switch (brightness) {
        case DISPLAY_BRIGHTNESS_OFF:
            power_state = SSD1306_POWER_OFF;
            break;
        case DISPLAY_BRIGHTNESS_DIM:
            power_state = SSD1306_POWER_DIM;
            break;
        case DISPLAY_BRIGHTNESS_FULL:
            power_state = SSD1306_POWER_FULL;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ssd1306_set_power(&displays[display], power_state);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Display %d brightness set to %d", display + 1, brightness);
    }

    return ret;
}

esp_err_t display_manager_set_all_brightness(display_brightness_t brightness)
{
    esp_err_t ret = ESP_OK;

    for (int i = 0; i < DISPLAY_COUNT; i++) {
        if (display_initialized[i]) {
            esp_err_t temp_ret = display_manager_set_brightness(i, brightness);
            if (temp_ret != ESP_OK) {
                ret = temp_ret;  // Capture error but continue with other displays
            }
        }
    }

    if (ret == ESP_OK) {
        current_brightness = brightness;
    }

    return ret;
}

esp_err_t display_manager_cycle_brightness(void)
{
    display_brightness_t next_brightness;

    // Cycle through: FULL → DIM → OFF → FULL
    switch (current_brightness) {
        case DISPLAY_BRIGHTNESS_FULL:
            next_brightness = DISPLAY_BRIGHTNESS_DIM;
            ESP_LOGI(TAG, "Cycling brightness: FULL → DIM");
            break;
        case DISPLAY_BRIGHTNESS_DIM:
            next_brightness = DISPLAY_BRIGHTNESS_OFF;
            ESP_LOGI(TAG, "Cycling brightness: DIM → OFF");
            break;
        case DISPLAY_BRIGHTNESS_OFF:
            next_brightness = DISPLAY_BRIGHTNESS_FULL;
            ESP_LOGI(TAG, "Cycling brightness: OFF → FULL");
            break;
        default:
            next_brightness = DISPLAY_BRIGHTNESS_FULL;
            break;
    }

    return display_manager_set_all_brightness(next_brightness);
}

ssd1306_handle_t* display_manager_get_display(display_id_t display)
{
    if (display >= DISPLAY_COUNT) {
        return NULL;
    }

    if (!display_initialized[display]) {
        return NULL;
    }

    return &displays[display];
}

esp_err_t display_manager_update(display_id_t display)
{
    if (display >= DISPLAY_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!display_initialized[display]) {
        return ESP_ERR_INVALID_STATE;
    }

    return ssd1306_update(&displays[display]);
}

esp_err_t display_manager_update_all(void)
{
    esp_err_t ret = ESP_OK;

    for (int i = 0; i < DISPLAY_COUNT; i++) {
        if (display_initialized[i]) {
            esp_err_t temp_ret = ssd1306_update(&displays[i]);
            if (temp_ret != ESP_OK) {
                ret = temp_ret;  // Capture error but continue with other displays
            }
        }
    }

    return ret;
}

display_brightness_t display_manager_get_brightness(void)
{
    return current_brightness;
}

esp_err_t display_manager_cycle_screen(void)
{
    active_screen = (screen_type_t)((active_screen + 1) % SCREEN_COUNT);
    const char *names[] = {"GPS", "STATUS", "RANGING"};
    ESP_LOGI(TAG, "Screen → %s", names[active_screen]);
    return ESP_OK;
}

screen_type_t display_manager_get_active_screen(void)
{
    return active_screen;
}
