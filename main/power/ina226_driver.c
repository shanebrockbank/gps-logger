#include "ina226_driver.h"
#include "communication/i2c_bus.h"
#include "pin_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "INA226";

// INA226 device handle
static i2c_master_dev_handle_t ina226_handle = NULL;

// I2C timeout and retry configuration
#define INA226_I2C_TIMEOUT_MS  1000
#define INA226_MAX_RETRIES     3

// INA226 Register Addresses
#define INA226_REG_CONFIG           0x00
#define INA226_REG_SHUNT_VOLTAGE    0x01
#define INA226_REG_BUS_VOLTAGE      0x02
#define INA226_REG_POWER            0x03
#define INA226_REG_CURRENT          0x04
#define INA226_REG_CALIBRATION      0x05
#define INA226_REG_MANUFACTURER_ID  0xFE
#define INA226_REG_DIE_ID           0xFF

// INA226 Configuration
#define INA226_SHUNT_RESISTOR_MOHM  100  // 0.1Ω = 100mΩ (typical for CJMCU-226)
#define INA226_MAX_CURRENT_A        3.2   // Maximum expected current

// Forward declarations
static esp_err_t ina226_read_register(uint8_t reg, uint16_t *value);
static esp_err_t ina226_verify_device(void);
static esp_err_t ina226_configure(void);
static esp_err_t ina226_calibrate(void);

static esp_err_t ina226_verify_device(void)
{
    uint16_t manufacturer_id, die_id;

    // Read manufacturer ID
    esp_err_t ret = ina226_read_register(INA226_REG_MANUFACTURER_ID, &manufacturer_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read manufacturer ID: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify manufacturer ID is Texas Instruments (0x5449 = "TI")
    if (manufacturer_id != 0x5449) {
        ESP_LOGE(TAG, "Wrong manufacturer ID: 0x%04X (expected 0x5449)", manufacturer_id);
        return ESP_ERR_NOT_FOUND;
    }

    // Read die ID
    ret = ina226_read_register(INA226_REG_DIE_ID, &die_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read die ID, continuing anyway");
        return ESP_OK;  // Not critical
    }

    // Warn if unexpected die ID
    if (die_id != 0x2260) {
        ESP_LOGW(TAG, "Unexpected die ID: 0x%04X (expected 0x2260)", die_id);
    }

    return ESP_OK;
}

static esp_err_t ina226_configure(void)
{
    SemaphoreHandle_t mutex = i2c_bus_get_mutex();
    if (mutex == NULL) {
        ESP_LOGW(TAG, "I2C mutex not available");
        return ESP_ERR_INVALID_STATE;
    }

    // Acquire PM lock to prevent light sleep during I2C transaction
    i2c_bus_acquire_pm_lock();

    // Acquire I2C bus mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(INA226_I2C_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire I2C mutex for configure");
        i2c_bus_release_pm_lock();
        return ESP_ERR_TIMEOUT;
    }

    // Averages=1, Bus/Shunt conv time=1.1ms, Continuous mode
    uint8_t config_data[3] = {INA226_REG_CONFIG, 0x41, 0x27};
    esp_err_t ret = i2c_master_transmit(ina226_handle, config_data, 3, pdMS_TO_TICKS(INA226_I2C_TIMEOUT_MS));

    // Release mutex
    xSemaphoreGive(mutex);

    // Release PM lock
    i2c_bus_release_pm_lock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure INA226: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t ina226_calibrate(void)
{
    SemaphoreHandle_t mutex = i2c_bus_get_mutex();
    if (mutex == NULL) {
        ESP_LOGW(TAG, "I2C mutex not available");
        return ESP_ERR_INVALID_STATE;
    }

    // Cal = 0.00512 / (Current_LSB * R_shunt)
    float current_lsb = INA226_MAX_CURRENT_A / 32768.0f;
    uint16_t cal_value = (uint16_t)(0.00512f / (current_lsb * (INA226_SHUNT_RESISTOR_MOHM / 1000.0f)));

    uint8_t cal_data[3] = {
        INA226_REG_CALIBRATION,
        (uint8_t)(cal_value >> 8),
        (uint8_t)(cal_value & 0xFF)
    };

    // Acquire PM lock to prevent light sleep during I2C transaction
    i2c_bus_acquire_pm_lock();

    // Acquire I2C bus mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(INA226_I2C_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire I2C mutex for calibrate");
        i2c_bus_release_pm_lock();
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = i2c_master_transmit(ina226_handle, cal_data, 3, pdMS_TO_TICKS(INA226_I2C_TIMEOUT_MS));

    // Release mutex
    xSemaphoreGive(mutex);

    // Release PM lock
    i2c_bus_release_pm_lock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write calibration: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t ina226_init(i2c_master_bus_handle_t bus_handle)
{
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Add INA226 device to I2C bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_config, &ina226_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add INA226 device: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ina226_verify_device();
    if (ret != ESP_OK) return ret;

    ret = ina226_configure();
    if (ret != ESP_OK) return ret;

    ret = ina226_calibrate();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "INA226 initialized (addr=0x%02X, shunt=%dmΩ, max_current=%.1fA)",
             INA226_I2C_ADDR, INA226_SHUNT_RESISTOR_MOHM, INA226_MAX_CURRENT_A);

    return ESP_OK;
}

static esp_err_t ina226_read_register(uint8_t reg, uint16_t *value)
{
    if (ina226_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t mutex = i2c_bus_get_mutex();
    if (mutex == NULL) {
        ESP_LOGW(TAG, "I2C mutex not available");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_FAIL;
    uint8_t data[2];

    // Acquire PM lock to prevent light sleep during I2C transaction
    i2c_bus_acquire_pm_lock();

    // Retry loop
    for (int attempt = 0; attempt < INA226_MAX_RETRIES; attempt++) {
        // Acquire I2C bus mutex
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(INA226_I2C_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to acquire I2C mutex (attempt %d/%d)", attempt + 1, INA226_MAX_RETRIES);
            continue;
        }

        // Perform I2C transaction
        ret = i2c_master_transmit_receive(ina226_handle, &reg, 1, data, 2, pdMS_TO_TICKS(INA226_I2C_TIMEOUT_MS));

        // Release mutex
        xSemaphoreGive(mutex);

        // Check if successful
        if (ret == ESP_OK) {
            *value = (data[0] << 8) | data[1];
            i2c_bus_release_pm_lock();
            return ESP_OK;
        }

        // Log retry (only if not last attempt)
        if (attempt < INA226_MAX_RETRIES - 1) {
            ESP_LOGD(TAG, "I2C read retry %d/%d (reg=0x%02X, err=%s)",
                     attempt + 1, INA226_MAX_RETRIES, reg, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));  // Brief delay before retry
        }
    }

    // Release PM lock before returning error
    i2c_bus_release_pm_lock();

    ESP_LOGW(TAG, "I2C read failed after %d attempts (reg=0x%02X): %s",
             INA226_MAX_RETRIES, reg, esp_err_to_name(ret));
    return ret;
}

esp_err_t ina226_read_current(float *current_ma)
{
    if (current_ma == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw_value;
    esp_err_t ret = ina226_read_register(INA226_REG_CURRENT, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }

    // Convert to milliamps
    float current_lsb = INA226_MAX_CURRENT_A / 32768.0f;
    int16_t signed_value = (int16_t)raw_value;
    *current_ma = signed_value * current_lsb * 1000.0f;

    return ESP_OK;
}

esp_err_t ina226_read_voltage(float *voltage_v)
{
    if (voltage_v == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw_value;
    esp_err_t ret = ina226_read_register(INA226_REG_BUS_VOLTAGE, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }

    // INA226 bus voltage: all 16 bits are used (unlike INA219 which uses bits [15:3])
    *voltage_v = raw_value * 0.00125f;  // LSB = 1.25mV

    return ESP_OK;
}

esp_err_t ina226_read_power(float *power_mw)
{
    if (power_mw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw_value;
    esp_err_t ret = ina226_read_register(INA226_REG_POWER, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }

    // Power LSB = 25 * Current_LSB
    float current_lsb = INA226_MAX_CURRENT_A / 32768.0f;
    *power_mw = raw_value * 25.0f * current_lsb * 1000.0f;

    return ESP_OK;
}
