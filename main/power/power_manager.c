#include "power_manager.h"
#include "ina226_driver.h"
#include "communication/i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "PWR_MGR";

// ========================================================================
// Power Manager Initialization (Orchestrator)
// ========================================================================

esp_err_t power_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing power manager...");

    // Initialize shared I2C bus (from communication module)
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus");
        return ret;
    }

    // Scan I2C bus for devices
    uint8_t device_count = 0;
    uint8_t addresses[10];
    ret = i2c_bus_scan(&device_count, addresses, 10);
    if (ret == ESP_OK && device_count > 0) {
        ESP_LOGI(TAG, "Found %d I2C device(s)", device_count);
        for (uint8_t i = 0; i < device_count; i++) {
            ESP_LOGI(TAG, "  - Device %d: 0x%02X", i + 1, addresses[i]);
        }
    }

    // Initialize INA226 current monitor
    ret = ina226_init(i2c_bus_get_handle());
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize INA226 (may not be connected)");
        // Don't fail - INA226 is optional for development
    }

    ESP_LOGI(TAG, "Power manager initialized");
    return ESP_OK;
}

// Future power control functions:
// - GPS power control (enable/disable GPS module)
// - Display power control (brightness/sleep)
// - Wireless module power control
// - SD card power management
