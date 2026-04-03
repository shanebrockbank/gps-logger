#include "power_modes.h"
#include "pin_config.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PWR_MODES";

// Current power mode
static power_mode_t current_mode = POWER_MODE_ACTIVE;

// Power mode configurations
typedef struct {
    uint32_t max_freq_mhz;
    uint32_t min_freq_mhz;
    bool light_sleep_enable;
    const char *name;
} power_mode_config_t;

static const power_mode_config_t mode_configs[] = {
    [POWER_MODE_ACTIVE]      = { 240, 240, false, "ACTIVE" },       // No scaling, no sleep
    [POWER_MODE_PERFORMANCE] = { 240,  40, true,  "PERFORMANCE" },  // Scale 240-40MHz with light sleep
    [POWER_MODE_BALANCED]    = { 160,  40, true,  "BALANCED" },     // Scale 160-40MHz with light sleep
    [POWER_MODE_LOW_POWER]   = { 80,   40, true,  "LOW_POWER" },    // Scale 80-40MHz with light sleep
};

esp_err_t power_modes_init(void)
{
    ESP_LOGI(TAG, "Initializing power management...");

    // Configure power management with default active mode (240MHz)
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 240,
        .light_sleep_enable = false,
    };

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power management: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Power management initialized (mode: ACTIVE, 240MHz)");
    return ESP_OK;
}

esp_err_t power_modes_set_mode(power_mode_t mode)
{
    if (mode >= sizeof(mode_configs) / sizeof(mode_configs[0])) {
        return ESP_ERR_INVALID_ARG;
    }

    const power_mode_config_t *config = &mode_configs[mode];

    esp_pm_config_t pm_config = {
        .max_freq_mhz = config->max_freq_mhz,
        .min_freq_mhz = config->min_freq_mhz,
        .light_sleep_enable = config->light_sleep_enable,
    };

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set power mode %s: %s", config->name, esp_err_to_name(ret));
        return ret;
    }

    current_mode = mode;
    ESP_LOGI(TAG, "Power mode set to %s (max=%dMHz, min=%dMHz, light_sleep=%d)",
             config->name, config->max_freq_mhz, config->min_freq_mhz, config->light_sleep_enable);

    // Allow I2C bus and peripherals to stabilize after frequency/power mode change
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

power_mode_t power_modes_get_current(void)
{
    return current_mode;
}

const char* power_modes_get_name(power_mode_t mode)
{
    if (mode >= sizeof(mode_configs) / sizeof(mode_configs[0])) {
        return "UNKNOWN";
    }
    return mode_configs[mode].name;
}

esp_err_t power_modes_set_cpu_freq(uint32_t freq_mhz)
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = freq_mhz,
        .min_freq_mhz = freq_mhz,
        .light_sleep_enable = false,
    };

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set CPU frequency to %dMHz: %s", freq_mhz, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "CPU frequency set to %dMHz", freq_mhz);
    return ESP_OK;
}

uint32_t power_modes_get_cpu_freq(void)
{
    const power_mode_config_t *config = &mode_configs[current_mode];
    return config->max_freq_mhz;
}

esp_err_t power_modes_enable_light_sleep(bool enable)
{
    const power_mode_config_t *config = &mode_configs[current_mode];

    esp_pm_config_t pm_config = {
        .max_freq_mhz = config->max_freq_mhz,
        .min_freq_mhz = config->min_freq_mhz,
        .light_sleep_enable = enable,
    };

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure light sleep: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Light sleep %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t power_modes_enter_deep_sleep(uint64_t sleep_time_us, int wake_gpio)
{
    ESP_LOGI(TAG, "Entering deep sleep (wake_gpio=%d)", wake_gpio);

    // Configure wake sources
    if (wake_gpio >= 0) {
        // Configure GPIO wake-up
        esp_err_t ret = esp_sleep_enable_ext0_wakeup(wake_gpio, 0); // Wake on LOW
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure GPIO wake-up: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Deep sleep wake on GPIO %d (LOW)", wake_gpio);
    }

    // Configure timer wake-up if specified
    if (sleep_time_us > 0) {
        esp_sleep_enable_timer_wakeup(sleep_time_us);
        ESP_LOGI(TAG, "Deep sleep timer wake-up: %llu us", sleep_time_us);
    }

    // Enter deep sleep
    ESP_LOGI(TAG, "Entering deep sleep now...");
    esp_deep_sleep_start();

    // Should never reach here
    return ESP_OK;
}
