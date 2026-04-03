#include "ssd1306_driver.h"
#include "communication/i2c_bus.h"
#include "pin_config.h"
#include "esp_log.h"
#include "esp_check.h"
#include <string.h>

// Logging tag - immutable, no thread safety needed
static const char *TAG = "SSD1306";

// I2C timeout configuration
#define SSD1306_I2C_TIMEOUT_MS  1000

// Stack buffer size for I2C transfers (avoids heap allocation for small writes)
#define SSD1306_STACK_BUFFER_SIZE  256

// SSD1306 Commands
#define SSD1306_CMD_SET_CONTRAST            0x81
#define SSD1306_CMD_DISPLAY_ALL_ON_RESUME   0xA4
#define SSD1306_CMD_DISPLAY_ALL_ON          0xA5
#define SSD1306_CMD_NORMAL_DISPLAY          0xA6
#define SSD1306_CMD_INVERT_DISPLAY          0xA7
#define SSD1306_CMD_DISPLAY_OFF             0xAE
#define SSD1306_CMD_DISPLAY_ON              0xAF
#define SSD1306_CMD_SET_DISPLAY_OFFSET      0xD3
#define SSD1306_CMD_SET_COM_PINS            0xDA
#define SSD1306_CMD_SET_VCOM_DETECT         0xDB
#define SSD1306_CMD_SET_DISPLAY_CLOCK_DIV   0xD5
#define SSD1306_CMD_SET_PRECHARGE           0xD9
#define SSD1306_CMD_SET_MULTIPLEX           0xA8
#define SSD1306_CMD_SET_LOW_COLUMN          0x00
#define SSD1306_CMD_SET_HIGH_COLUMN         0x10
#define SSD1306_CMD_SET_START_LINE          0x40
#define SSD1306_CMD_MEMORY_MODE             0x20
#define SSD1306_CMD_COLUMN_ADDR             0x21
#define SSD1306_CMD_PAGE_ADDR               0x22
#define SSD1306_CMD_COM_SCAN_INC            0xC0
#define SSD1306_CMD_COM_SCAN_DEC            0xC8
#define SSD1306_CMD_SEG_REMAP               0xA0
#define SSD1306_CMD_CHARGE_PUMP             0x8D
#define SSD1306_CMD_EXTERNAL_VCC            0x01
#define SSD1306_CMD_SWITCH_CAP_VCC          0x02

// Control bytes
#define SSD1306_CONTROL_CMD_STREAM          0x00
#define SSD1306_CONTROL_DATA_STREAM         0x40

// SSD1306 configuration values
#define SSD1306_CLOCK_DIV_DEFAULT      0x80  // Clock divide ratio/oscillator frequency
#define SSD1306_DISPLAY_OFFSET_DEFAULT 0x00  // No vertical offset
#define SSD1306_START_LINE_DEFAULT     0x00  // Start at line 0
#define SSD1306_CHARGE_PUMP_ENABLE     0x14  // Enable charge pump (0x10 = disable)
#define SSD1306_MEMORY_MODE_HORIZONTAL 0x00  // Horizontal addressing mode
#define SSD1306_SEG_REMAP_FLIP         0x01  // Flip segment remap horizontally
#define SSD1306_COM_PINS_CONFIG        0x12  // COM pins hardware config for 128x64
#define SSD1306_CONTRAST_DEFAULT       0x7F  // Medium contrast (0-255 range)
#define SSD1306_CONTRAST_DIM           0x10  // Low contrast for dim mode
#define SSD1306_CONTRAST_FULL          0xCF  // High contrast for full brightness
#define SSD1306_PRECHARGE_PERIOD       0xF1  // Pre-charge period
#define SSD1306_VCOM_DESELECT          0x40  // VCOM deselect level

// Simple 5x7 bitmap font (ASCII 32-126)
// Each character is 5 bytes wide, 8 pixels tall (7 used for character, 1 for spacing)
// Immutable lookup table - no thread safety needed
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // (space)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x00, 0x7F, 0x41, 0x41}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x41, 0x41, 0x7F, 0x00, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x00, 0x7F, 0x10, 0x28, 0x44}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x08, 0x04, 0x08, 0x10, 0x08}, // ~
};

// Forward declarations
static esp_err_t ssd1306_write_cmd(ssd1306_handle_t *handle, uint8_t cmd);
static esp_err_t ssd1306_write_data(ssd1306_handle_t *handle, const uint8_t *data, size_t len);

static esp_err_t ssd1306_write_cmd(ssd1306_handle_t *handle, uint8_t cmd)
{
    if (handle == NULL || handle->i2c_dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t mutex = i2c_bus_get_mutex();
    if (mutex == NULL) {
        ESP_LOGW(TAG, "I2C mutex not available");
        return ESP_ERR_INVALID_STATE;
    }

    // Acquire PM lock to prevent light sleep during I2C transaction
    i2c_bus_acquire_pm_lock();

    // Acquire I2C bus mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(SSD1306_I2C_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire I2C mutex for write_cmd");
        i2c_bus_release_pm_lock();
        return ESP_ERR_TIMEOUT;
    }

    uint8_t data[2] = {SSD1306_CONTROL_CMD_STREAM, cmd};
    esp_err_t ret = i2c_master_transmit(handle->i2c_dev, data, 2, pdMS_TO_TICKS(SSD1306_I2C_TIMEOUT_MS));

    // Release mutex
    xSemaphoreGive(mutex);

    // Release PM lock
    i2c_bus_release_pm_lock();

    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to write command 0x%02X: %s", cmd, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t ssd1306_write_data(ssd1306_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || handle->i2c_dev == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t mutex = i2c_bus_get_mutex();
    if (mutex == NULL) {
        ESP_LOGW(TAG, "I2C mutex not available");
        return ESP_ERR_INVALID_STATE;
    }

    // Acquire PM lock to prevent light sleep during I2C transaction
    i2c_bus_acquire_pm_lock();

    // Use stack buffer for small transfers to avoid heap allocation
    uint8_t stack_buffer[SSD1306_STACK_BUFFER_SIZE];
    esp_err_t ret = ESP_OK;

    // For small transfers, use stack buffer (most common case)
    if (len + 1 <= SSD1306_STACK_BUFFER_SIZE) {
        stack_buffer[0] = SSD1306_CONTROL_DATA_STREAM;
        memcpy(&stack_buffer[1], data, len);

        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(SSD1306_I2C_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to acquire I2C mutex for write_data");
            i2c_bus_release_pm_lock();
            return ESP_ERR_TIMEOUT;
        }

        ret = i2c_master_transmit(handle->i2c_dev, stack_buffer, len + 1,
                                 pdMS_TO_TICKS(SSD1306_I2C_TIMEOUT_MS));
        xSemaphoreGive(mutex);
        i2c_bus_release_pm_lock();
        return ret;
    }

    // For large transfers, chunk into multiple writes using stack buffer
    size_t offset = 0;
    const size_t chunk_size = SSD1306_STACK_BUFFER_SIZE - 1;  // Leave room for control byte

    while (offset < len && ret == ESP_OK) {
        size_t transfer_len = (len - offset > chunk_size) ? chunk_size : (len - offset);

        stack_buffer[0] = SSD1306_CONTROL_DATA_STREAM;
        memcpy(&stack_buffer[1], &data[offset], transfer_len);

        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(SSD1306_I2C_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to acquire I2C mutex for write_data (chunk %zu)", offset);
            i2c_bus_release_pm_lock();
            return ESP_ERR_TIMEOUT;
        }

        ret = i2c_master_transmit(handle->i2c_dev, stack_buffer, transfer_len + 1,
                                 pdMS_TO_TICKS(SSD1306_I2C_TIMEOUT_MS));
        xSemaphoreGive(mutex);

        offset += transfer_len;
    }

    // Release PM lock after all chunks are done
    i2c_bus_release_pm_lock();

    return ret;
}

esp_err_t ssd1306_init(ssd1306_handle_t *handle, uint8_t i2c_address)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get I2C bus handle
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();
    if (bus == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Add device to I2C bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_config, &handle->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SSD1306 device at 0x%02X: %s", i2c_address, esp_err_to_name(ret));
        return ret;
    }

    handle->address = i2c_address;
    handle->power_state = SSD1306_POWER_OFF;

    // Clear frame buffer
    memset(handle->buffer, 0, sizeof(handle->buffer));

    // SSD1306 initialization sequence
    ESP_LOGI(TAG, "Initializing SSD1306 at address 0x%02X...", i2c_address);

    // Display off
    ssd1306_write_cmd(handle, SSD1306_CMD_DISPLAY_OFF);

    // Set display clock divide ratio
    ssd1306_write_cmd(handle, SSD1306_CMD_SET_DISPLAY_CLOCK_DIV);
    ssd1306_write_cmd(handle, SSD1306_CLOCK_DIV_DEFAULT);

    // Set multiplex ratio
    ssd1306_write_cmd(handle, SSD1306_CMD_SET_MULTIPLEX);
    ssd1306_write_cmd(handle, SSD1306_HEIGHT - 1);

    // Set display offset
    ssd1306_write_cmd(handle, SSD1306_CMD_SET_DISPLAY_OFFSET);
    ssd1306_write_cmd(handle, SSD1306_DISPLAY_OFFSET_DEFAULT);

    // Set start line
    ssd1306_write_cmd(handle, SSD1306_CMD_SET_START_LINE | SSD1306_START_LINE_DEFAULT);

    // Enable charge pump
    ssd1306_write_cmd(handle, SSD1306_CMD_CHARGE_PUMP);
    ssd1306_write_cmd(handle, SSD1306_CHARGE_PUMP_ENABLE);

    // Set memory addressing mode to horizontal
    ssd1306_write_cmd(handle, SSD1306_CMD_MEMORY_MODE);
    ssd1306_write_cmd(handle, SSD1306_MEMORY_MODE_HORIZONTAL);

    // Set segment remap (flip horizontally)
    ssd1306_write_cmd(handle, SSD1306_CMD_SEG_REMAP | SSD1306_SEG_REMAP_FLIP);

    // Set COM output scan direction (flip vertically)
    ssd1306_write_cmd(handle, SSD1306_CMD_COM_SCAN_DEC);

    // Set COM pins hardware configuration
    ssd1306_write_cmd(handle, SSD1306_CMD_SET_COM_PINS);
    ssd1306_write_cmd(handle, SSD1306_COM_PINS_CONFIG);

    // Set contrast (default medium)
    ssd1306_write_cmd(handle, SSD1306_CMD_SET_CONTRAST);
    ssd1306_write_cmd(handle, SSD1306_CONTRAST_DEFAULT);

    // Set pre-charge period
    ssd1306_write_cmd(handle, SSD1306_CMD_SET_PRECHARGE);
    ssd1306_write_cmd(handle, SSD1306_PRECHARGE_PERIOD);

    // Set VCOM deselect level
    ssd1306_write_cmd(handle, SSD1306_CMD_SET_VCOM_DETECT);
    ssd1306_write_cmd(handle, SSD1306_VCOM_DESELECT);

    // Display all on resume
    ssd1306_write_cmd(handle, SSD1306_CMD_DISPLAY_ALL_ON_RESUME);

    // Normal display (not inverted)
    ssd1306_write_cmd(handle, SSD1306_CMD_NORMAL_DISPLAY);

    // Display on
    ssd1306_write_cmd(handle, SSD1306_CMD_DISPLAY_ON);

    handle->power_state = SSD1306_POWER_FULL;

    ESP_LOGI(TAG, "SSD1306 initialized successfully at 0x%02X", i2c_address);

    return ESP_OK;
}

esp_err_t ssd1306_set_power(ssd1306_handle_t *handle, ssd1306_power_t power)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (power) {
        case SSD1306_POWER_OFF:
            ESP_RETURN_ON_ERROR(ssd1306_write_cmd(handle, SSD1306_CMD_DISPLAY_OFF),
                              TAG, "Display OFF command failed");
            break;

        case SSD1306_POWER_DIM:
            ESP_RETURN_ON_ERROR(ssd1306_write_cmd(handle, SSD1306_CMD_DISPLAY_ON),
                              TAG, "Display ON command failed");
            ESP_RETURN_ON_ERROR(ssd1306_write_cmd(handle, SSD1306_CMD_SET_CONTRAST),
                              TAG, "Set contrast command failed");
            ESP_RETURN_ON_ERROR(ssd1306_write_cmd(handle, SSD1306_CONTRAST_DIM),
                              TAG, "Set dim value failed");
            break;

        case SSD1306_POWER_FULL:
            ESP_RETURN_ON_ERROR(ssd1306_write_cmd(handle, SSD1306_CMD_DISPLAY_ON),
                              TAG, "Display ON command failed");
            ESP_RETURN_ON_ERROR(ssd1306_write_cmd(handle, SSD1306_CMD_SET_CONTRAST),
                              TAG, "Set contrast command failed");
            ESP_RETURN_ON_ERROR(ssd1306_write_cmd(handle, SSD1306_CONTRAST_FULL),
                              TAG, "Set full value failed");
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    handle->power_state = power;
    ESP_LOGI(TAG, "Display 0x%02X power state: %d", handle->address, power);

    return ESP_OK;
}

esp_err_t ssd1306_free(ssd1306_handle_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->i2c_dev != NULL) {
        i2c_master_bus_rm_device(handle->i2c_dev);
        handle->i2c_dev = NULL;
    }

    return ESP_OK;
}

esp_err_t ssd1306_update(ssd1306_handle_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set column address range (0-127)
    ssd1306_write_cmd(handle, SSD1306_CMD_COLUMN_ADDR);
    ssd1306_write_cmd(handle, 0);
    ssd1306_write_cmd(handle, SSD1306_WIDTH - 1);

    // Set page address range (0-7 for 64-pixel height)
    ssd1306_write_cmd(handle, SSD1306_CMD_PAGE_ADDR);
    ssd1306_write_cmd(handle, 0);
    ssd1306_write_cmd(handle, (SSD1306_HEIGHT / 8) - 1);

    // Send entire frame buffer
    return ssd1306_write_data(handle, handle->buffer, sizeof(handle->buffer));
}

esp_err_t ssd1306_clear(ssd1306_handle_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle->buffer, 0, sizeof(handle->buffer));
    return ESP_OK;
}

esp_err_t ssd1306_set_pixel(ssd1306_handle_t *handle, uint8_t x, uint8_t y, bool on)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t index = x + (y / 8) * SSD1306_WIDTH;
    uint8_t bit = y % 8;

    if (on) {
        handle->buffer[index] |= (1 << bit);
    } else {
        handle->buffer[index] &= ~(1 << bit);
    }

    return ESP_OK;
}

esp_err_t ssd1306_draw_char(ssd1306_handle_t *handle, uint8_t x, uint8_t y, char c)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (c < 32 || c > 126) {
        return ESP_ERR_INVALID_ARG;  // Out of font range
    }

    const uint8_t *glyph = font5x7[c - 32];

    for (uint8_t col = 0; col < 5; col++) {
        if (x + col >= SSD1306_WIDTH) break;

        uint8_t column_data = glyph[col];
        for (uint8_t row = 0; row < 8; row++) {
            if (y + row >= SSD1306_HEIGHT) break;
            ssd1306_set_pixel(handle, x + col, y + row, (column_data & (1 << row)) != 0);
        }
    }

    return ESP_OK;
}

esp_err_t ssd1306_draw_string(ssd1306_handle_t *handle, uint8_t x, uint8_t y, const char *str)
{
    if (handle == NULL || str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cursor_x = x;

    while (*str) {
        if (cursor_x + 6 > SSD1306_WIDTH) {
            break;  // Stop if string exceeds display width
        }

        ssd1306_draw_char(handle, cursor_x, y, *str);
        cursor_x += 6;  // 5 pixels for char + 1 pixel spacing
        str++;
    }

    return ESP_OK;
}

esp_err_t ssd1306_draw_line(ssd1306_handle_t *handle, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Bresenham's line algorithm
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        ssd1306_set_pixel(handle, x0, y0, on);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    return ESP_OK;
}
