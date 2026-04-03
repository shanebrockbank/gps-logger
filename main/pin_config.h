#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#include "driver/gpio.h"
// #include "driver/uart.h"  // Uncomment when GPS is added in Phase 2B
#include "driver/i2c_master.h"

// =============================================================================
// BUTTON INPUTS
// =============================================================================
// All buttons use internal pull-ups, connect to GND when pressed

#define BUTTON_1_GPIO     32  // Button 1: Cycle display modes/brightness
#define BUTTON_2_GPIO     33  // Button 2: Toggle active/idle states
#define BUTTON_3_GPIO     27  // Button 3: Return to light sleep (standby)

#define BUTTON_DEBOUNCE_MS  50  // Debounce time in milliseconds

// =============================================================================
// I2C BUS CONFIGURATION
// =============================================================================
// Shared I2C bus for displays and power monitor

#define I2C_MASTER_NUM        I2C_NUM_0
#define I2C_SDA_GPIO          21
#define I2C_SCL_GPIO          22
#define I2C_FREQ_HZ           100000  // 100kHz standard mode 


// I2C Device Addresses
#define DISPLAY_1_I2C_ADDR    0x3C  // SSD1306 OLED Display 1
#define DISPLAY_2_I2C_ADDR    0x3D  // SSD1306 OLED Display 2
#define INA226_I2C_ADDR       0x40  // INA226 Current/Power Monitor (default)

// =============================================================================
// UART CONFIGURATION (GPS)
// =============================================================================
// NEO-M8N GPS Module

#define GPS_UART_NUM          UART_NUM_2
#define GPS_UART_TX_GPIO      17  // ESP32 TX → GPS RX (optional, for configuration)
#define GPS_UART_RX_GPIO      16  // ESP32 RX ← GPS TX
#define GPS_UART_BAUD_RATE    9600  // NEO-M8N default baud rate

// =============================================================================
// SPI BUS CONFIGURATION (Future: LoRa + SD Card)
// =============================================================================

#define SPI_MOSI_GPIO         23
#define SPI_MISO_GPIO         19
#define SPI_SCLK_GPIO         18

// SPI Chip Select Pins
#define LORA_CS_GPIO          5   // LoRa module CS
#define SD_CARD_CS_GPIO       15  // SD card CS

// LoRa Additional Pins
#define LORA_RESET_GPIO       2   // LoRa reset pin
#define LORA_DIO0_GPIO        4   // LoRa DIO0 interrupt pin

// =============================================================================
// STATUS LED (Optional)
// =============================================================================

#define STATUS_LED_GPIO       25  // PWM-capable for breathing effect

// =============================================================================
// RESERVED/AVOID PINS
// =============================================================================
// GPIO0: Boot mode (avoid)
// GPIO2: Boot mode (used for LoRa reset, OK)
// GPIO12: Boot voltage (avoid if possible)
// GPIO15: Boot mode (used for SD CS, OK with pull-up)
// GPIO1/3: UART0 (USB/serial debug)
// GPIO6-11: Flash (NEVER use)
// GPIO34-39: Input only (no pull-up/pull-down)

#endif // PIN_CONFIG_H
