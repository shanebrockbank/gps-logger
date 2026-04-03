#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include "esp_err.h"
#include "pin_config.h"

// Button handler functions
esp_err_t button_handler_init(void);
void button_handler_task(void *pvParameters);

#endif // BUTTON_HANDLER_H
