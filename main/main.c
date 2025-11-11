#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "GPS Logger Starting...");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    int counter = 0;
    while (1) {
        ESP_LOGI(TAG, "Heartbeat: %d", counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
