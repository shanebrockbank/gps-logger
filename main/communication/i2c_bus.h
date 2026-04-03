#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// I2C bus initialization and management
esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_get_handle(void);
SemaphoreHandle_t i2c_bus_get_mutex(void);

// Power management locks for I2C transactions
esp_err_t i2c_bus_acquire_pm_lock(void);
void i2c_bus_release_pm_lock(void);

// I2C bus scanning - detects devices on the bus
esp_err_t i2c_bus_scan(uint8_t *device_count, uint8_t *addresses, uint8_t max_devices);

#endif // I2C_BUS_H
