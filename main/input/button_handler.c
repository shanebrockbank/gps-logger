#include "button_handler.h"
#include "system_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "BTN_HDLR";

// Queue for button events from ISR
static QueueHandle_t button_event_queue = NULL;

// Button event structure for ISR
typedef struct {
    uint32_t gpio_num;
    uint32_t timestamp;
} button_isr_event_t;

// ISR handler - posts button events to queue
static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    button_isr_event_t event = {
        .gpio_num = gpio_num,
        .timestamp = xTaskGetTickCountFromISR()
    };
    xQueueSendFromISR(button_event_queue, &event, NULL);
}

esp_err_t button_handler_init(void)
{
    // Create button event queue
    button_event_queue = xQueueCreate(5, sizeof(button_isr_event_t));
    if (button_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_FAIL;
    }

    // Install GPIO ISR service (must be called before adding handlers)
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure all three buttons
    const uint32_t button_gpios[] = {BUTTON_1_GPIO, BUTTON_2_GPIO, BUTTON_3_GPIO};
    const char* button_names[] = {"Button 1", "Button 2", "Button 3"};

    for (int i = 0; i < 3; i++) {
        gpio_config_t btn_config = {
            .pin_bit_mask = (1ULL << button_gpios[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE  // Interrupt on falling edge (button press)
        };
        gpio_config(&btn_config);
        gpio_isr_handler_add(button_gpios[i], button_isr_handler, (void*)button_gpios[i]);
        ESP_LOGD(TAG, "Configured %s (GPIO%lu)", button_names[i], button_gpios[i]);
    }

    ESP_LOGI(TAG, "Button handler initialized");
    return ESP_OK;
}

void button_handler_task(void *pvParameters)
{
    button_isr_event_t isr_event;
    static uint32_t last_button_time[3] = {0};  // Debounce tracking: [0]=Button1, [1]=Button2, [2]=Button3

    ESP_LOGI(TAG, "Button handler task started");

    while (1) {
        // Wait for button press from ISR
        if (!xQueueReceive(button_event_queue, &isr_event, portMAX_DELAY)) {
            continue;  // Should never happen with portMAX_DELAY
        }

        uint32_t current_time = xTaskGetTickCount();
        uint8_t button_index;
        event_type_t event_type;

        // Determine which button - use negative space pattern
        if (isr_event.gpio_num == BUTTON_1_GPIO) {
            button_index = 0;
            event_type = EVENT_BUTTON_1_PRESS;
        } else if (isr_event.gpio_num == BUTTON_2_GPIO) {
            button_index = 1;
            event_type = EVENT_BUTTON_2_PRESS;
        } else if (isr_event.gpio_num == BUTTON_3_GPIO) {
            button_index = 2;
            event_type = EVENT_BUTTON_3_PRESS;
        } else {
            // Unknown GPIO - ignore
            continue;
        }

        // Debounce check - early return pattern
        if ((current_time - last_button_time[button_index]) < pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
            ESP_LOGD(TAG, "Button %lu debounced", isr_event.gpio_num);
            continue;
        }

        last_button_time[button_index] = current_time;
        ESP_LOGI(TAG, "Button %lu pressed", isr_event.gpio_num);

        // Post event to system manager
        esp_err_t ret = system_manager_post_event(event_type, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to post button event to system manager");
        }
    }
}
