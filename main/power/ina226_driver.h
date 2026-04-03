#ifndef INA226_DRIVER_H
#define INA226_DRIVER_H

#include "esp_err.h"
#include "driver/i2c_master.h"

// INA226 initialization
esp_err_t ina226_init(i2c_master_bus_handle_t bus_handle);

// INA226 measurement functions
esp_err_t ina226_read_current(float *current_ma);
esp_err_t ina226_read_voltage(float *voltage_v);
esp_err_t ina226_read_power(float *power_mw);

#endif // INA226_DRIVER_H
