#include "i2c_bus.h"
#include "pin_config.h"
#include "esp_log.h"
#include "esp_pm.h"

// Logging tag - immutable, no thread safety needed
static const char *TAG = "I2C_BUS";

// I2C configuration constants
#define I2C_GLITCH_FILTER_COUNT  7    // Filter glitches < 7 SCL cycles
#define I2C_PROBE_TIMEOUT_MS     500  // Probe timeout per device
#define I2C_SCAN_MAX_DEVICES     10   // Maximum devices to detect during scan

// I2C bus shared resources - owned by i2c_bus module
// Thread safety: Protected by i2c_mutex - all I2C transactions must acquire mutex
// Used by: INA226 driver, SSD1306 driver, and any future I2C devices
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static SemaphoreHandle_t i2c_mutex = NULL;
static esp_pm_lock_handle_t i2c_pm_lock = NULL;

esp_err_t i2c_bus_init(void)
{
    if (i2c_bus_handle != NULL) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK;
    }

    // Create I2C mutex for shared access
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C mutex");
        return ESP_FAIL;
    }

    // Create power management lock to prevent light sleep during I2C transactions
    esp_err_t ret = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "i2c_bus", &i2c_pm_lock);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create PM lock: %s (continuing without PM lock)", esp_err_to_name(ret));
        i2c_pm_lock = NULL;  // Continue without PM lock
    } else {
        ESP_LOGI(TAG, "PM lock created for I2C bus");
    }

    // Configure I2C master bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_FILTER_COUNT,
        .flags.enable_internal_pullup = true,
    };

    ret = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d, %dkHz)",
             I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ_HZ / 1000);

    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return i2c_bus_handle;
}

SemaphoreHandle_t i2c_bus_get_mutex(void)
{
    return i2c_mutex;
}

esp_err_t i2c_bus_scan(uint8_t *device_count, uint8_t *addresses, uint8_t max_devices)
{
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (device_count == NULL || addresses == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (max_devices == 0) {
        return ESP_ERR_INVALID_ARG;  // Prevent zero-size buffer
    }

    ESP_LOGI(TAG, "Scanning I2C bus for devices...");

    *device_count = 0;

    // Scan all valid 7-bit I2C addresses (0x08 to 0x77)
    // Skip 0x00-0x07 (reserved) and 0x78-0x7F (reserved)
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        // Try to probe the device
        esp_err_t ret = i2c_master_probe(i2c_bus_handle, addr, I2C_PROBE_TIMEOUT_MS);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at address 0x%02X", addr);

            if (*device_count < max_devices) {
                addresses[*device_count] = addr;
                (*device_count)++;
            } else {
                ESP_LOGW(TAG, "Max device limit reached (%d), stopping scan", max_devices);
                break;
            }
        }
    }

    ESP_LOGI(TAG, "I2C scan complete: %d device(s) found", *device_count);

    return ESP_OK;
}

esp_err_t i2c_bus_acquire_pm_lock(void)
{
    if (i2c_pm_lock == NULL) {
        // PM lock not available (possibly failed to create), continue anyway
        return ESP_OK;
    }

    esp_err_t ret = esp_pm_lock_acquire(i2c_pm_lock);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to acquire PM lock: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

void i2c_bus_release_pm_lock(void)
{
    if (i2c_pm_lock == NULL) {
        // PM lock not available, nothing to release
        return;
    }

    esp_err_t ret = esp_pm_lock_release(i2c_pm_lock);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to release PM lock: %s", esp_err_to_name(ret));
    }
}
