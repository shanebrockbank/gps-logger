#include "battery_monitor.h"
#include "ina226_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BAT_MON";

// LiPo single-cell discharge curve (voltage → percentage)
typedef struct { float v; uint8_t pct; } lut_t;
static const lut_t LUT[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.90f, 70},
    {3.80f, 60},  {3.70f, 50}, {3.60f, 40}, {3.50f, 30},
    {3.40f, 20},  {3.30f, 10}, {3.20f,  5}, {3.00f,  0},
};
#define LUT_SIZE (sizeof(LUT) / sizeof(LUT[0]))

// Assumed average current draw for runtime estimation (mA)
#define AVG_CURRENT_MA   45
#define CAPACITY_MAH   2500
#define NUM_SAMPLES      4

static battery_info_t   g_info  = { .state = BATTERY_STATE_UNKNOWN };
static SemaphoreHandle_t g_mutex = NULL;
static float             g_prev_voltage = 0.0f;

static uint8_t voltage_to_percent(float v)
{
    if (v >= LUT[0].v) return 100;
    if (v <= LUT[LUT_SIZE - 1].v) return 0;

    for (size_t i = 0; i < LUT_SIZE - 1; i++) {
        if (v <= LUT[i].v && v >= LUT[i+1].v) {
            float ratio = (v - LUT[i+1].v) / (LUT[i].v - LUT[i+1].v);
            return (uint8_t)(LUT[i+1].pct + ratio * (LUT[i].pct - LUT[i+1].pct));
        }
    }
    return 0;
}

static battery_state_t percent_to_state(uint8_t pct)
{
    if (pct >= 90) return BATTERY_STATE_FULL;
    if (pct >= 50) return BATTERY_STATE_GOOD;
    if (pct >= 20) return BATTERY_STATE_NORMAL;
    if (pct >= 10) return BATTERY_STATE_LOW;
    return BATTERY_STATE_CRITICAL;
}

esp_err_t battery_monitor_init(void)
{
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) return ESP_ERR_NO_MEM;

    esp_err_t ret = battery_monitor_update();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Init: %.2fV %d%%", g_info.voltage_v, g_info.percent);
    }
    return ESP_OK; // non-fatal if first read fails
}

esp_err_t battery_monitor_update(void)
{
    float sum = 0.0f;
    uint8_t n = 0;

    for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
        float v = 0.0f;
        if (ina226_read_voltage(&v) == ESP_OK) { sum += v; n++; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (n == 0) return ESP_FAIL;

    float avg = sum / n;
    uint8_t pct = voltage_to_percent(avg);
    battery_state_t state = percent_to_state(pct);
    bool charging = (avg > g_prev_voltage + 0.02f);
    uint32_t mins = ((CAPACITY_MAH * pct / 100) * 60) / AVG_CURRENT_MA;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    g_info.voltage_v = avg;
    g_info.percent = pct;
    g_info.state = state;
    g_info.is_charging = charging;
    g_info.estimated_minutes = mins;
    xSemaphoreGive(g_mutex);

    g_prev_voltage = avg;
    ESP_LOGD(TAG, "%.2fV %d%% ~%dm %s", avg, pct, mins, battery_state_to_string(state));
    return ESP_OK;
}

esp_err_t battery_monitor_get_info(battery_info_t *info)
{
    if (!info || !g_mutex) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memcpy(info, &g_info, sizeof(battery_info_t));
    xSemaphoreGive(g_mutex);
    return ESP_OK;
}

uint8_t battery_monitor_get_percent(void)
{
    battery_info_t info = {0};
    return (battery_monitor_get_info(&info) == ESP_OK) ? info.percent : 0;
}

bool battery_monitor_is_low(void)      { return battery_monitor_get_percent() < BATTERY_LOW_THRESHOLD_PCT; }
bool battery_monitor_is_critical(void) { return battery_monitor_get_percent() < BATTERY_CRITICAL_THRESHOLD_PCT; }

const char *battery_state_to_string(battery_state_t s)
{
    switch (s) {
        case BATTERY_STATE_FULL:     return "FULL";
        case BATTERY_STATE_GOOD:     return "GOOD";
        case BATTERY_STATE_NORMAL:   return "NORMAL";
        case BATTERY_STATE_LOW:      return "LOW";
        case BATTERY_STATE_CRITICAL: return "CRITICAL";
        default:                     return "UNKNOWN";
    }
}
