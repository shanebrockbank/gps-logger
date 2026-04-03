/**
 * @file lora_manager.c
 * @brief LoRa manager for LR02 module (SX1276 chipset, 868/915 MHz)
 *
 * SPI communication: HSPI bus, GPIO CS/RESET/DIO0 per pin_config.h
 * LoRa settings: SF7, BW125kHz, CR 4/5, explicit header, 8-byte preamble
 * Expected range: >2 km line-of-sight at +17 dBm output power
 */

#include "lora_manager.h"
#include "pin_config.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "LORA";

// ── SX1276 register map ───────────────────────────────────────────────────────
#define REG_FIFO                0x00
#define REG_OP_MODE             0x01
#define REG_FRF_MSB             0x06
#define REG_FRF_MID             0x07
#define REG_FRF_LSB             0x08
#define REG_PA_CONFIG           0x09
#define REG_OCP                 0x0B
#define REG_LNA                 0x0C
#define REG_FIFO_ADDR_PTR       0x0D
#define REG_FIFO_TX_BASE_ADDR   0x0E
#define REG_FIFO_RX_BASE_ADDR   0x0F
#define REG_FIFO_RX_CURR_ADDR   0x10
#define REG_IRQ_FLAGS           0x12
#define REG_RX_NB_BYTES         0x13
#define REG_PKT_RSSI_VALUE      0x1A
#define REG_PKT_SNR_VALUE       0x19
#define REG_MODEM_CONFIG_1      0x1D
#define REG_MODEM_CONFIG_2      0x1E
#define REG_PREAMBLE_MSB        0x20
#define REG_PREAMBLE_LSB        0x21
#define REG_PAYLOAD_LENGTH      0x22
#define REG_MODEM_CONFIG_3      0x26
#define REG_DETECTION_OPTIMIZE  0x31
#define REG_DETECTION_THRESHOLD 0x37
#define REG_SYNC_WORD           0x39
#define REG_DIO_MAPPING_1       0x40
#define REG_VERSION             0x42

// ── SX1276 mode constants ─────────────────────────────────────────────────────
#define MODE_LONG_RANGE  0x80
#define MODE_SLEEP       0x00
#define MODE_STDBY       0x01
#define MODE_TX          0x03
#define MODE_RX_CONT     0x05

#define IRQ_RX_DONE      0x40
#define IRQ_TX_DONE      0x08
#define IRQ_CRC_ERROR    0x20

#define SX1276_VERSION   0x12

// ── Frequency lookup (FRF = F * 2^19 / 32e6) ─────────────────────────────────
static const uint8_t FREQ_REG[2][3] = {
    {0xD9, 0x00, 0x00},  // 868 MHz
    {0xE4, 0xC0, 0x00},  // 915 MHz
};

static const char *DEVICE_ID = "DEV1";

// ── Module state ──────────────────────────────────────────────────────────────
static spi_device_handle_t g_spi     = NULL;
static SemaphoreHandle_t   g_mutex   = NULL;
static protocol_stats_t    g_stats   = {0};
static bool                g_running = false;

// ── Low-level SPI helpers ─────────────────────────────────────────────────────

static uint8_t reg_read(uint8_t addr)
{
    uint8_t tx[2] = {addr & 0x7F, 0x00};
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length = 16, .tx_buffer = tx, .rx_buffer = rx
    };
    spi_device_polling_transmit(g_spi, &t);
    return rx[1];
}

static void reg_write(uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = {addr | 0x80, val};
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    spi_device_polling_transmit(g_spi, &t);
}

static void set_mode(uint8_t mode)
{
    reg_write(REG_OP_MODE, MODE_LONG_RANGE | mode);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ── Initialisation helpers ────────────────────────────────────────────────────

static esp_err_t spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num   = SPI_MISO_GPIO,
        .mosi_io_num   = SPI_MOSI_GPIO,
        .sclk_io_num   = SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 256,
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means already initialised — that's OK
        return ret;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = LORA_CS_GPIO,
        .queue_size     = 1,
    };
    return spi_bus_add_device(SPI2_HOST, &devcfg, &g_spi);
}

static esp_err_t sx1276_reset(void)
{
    gpio_set_direction(LORA_RESET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LORA_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t sx1276_configure(lora_freq_t freq)
{
    // Verify chip version
    uint8_t ver = reg_read(REG_VERSION);
    if (ver != SX1276_VERSION) {
        ESP_LOGE(TAG, "Bad version: 0x%02X (expected 0x%02X)", ver, SX1276_VERSION);
        return ESP_ERR_NOT_FOUND;
    }

    // Enter sleep, enable LoRa mode
    set_mode(MODE_SLEEP);

    // Set frequency
    reg_write(REG_FRF_MSB, FREQ_REG[freq][0]);
    reg_write(REG_FRF_MID, FREQ_REG[freq][1]);
    reg_write(REG_FRF_LSB, FREQ_REG[freq][2]);

    // PA: PA_BOOST pin, max power, +17 dBm
    reg_write(REG_PA_CONFIG, 0x8F);

    // OCP: enable at 100 mA
    reg_write(REG_OCP, 0x2B);

    // LNA: max gain, boost on
    reg_write(REG_LNA, 0x23);

    // Modem config 1: BW=125kHz, CR=4/5, explicit header
    reg_write(REG_MODEM_CONFIG_1, 0x72);

    // Modem config 2: SF7, TX single, CRC on
    reg_write(REG_MODEM_CONFIG_2, 0x74);

    // Modem config 3: auto AGC, low DR optimize off
    reg_write(REG_MODEM_CONFIG_3, 0x04);

    // Preamble length = 8
    reg_write(REG_PREAMBLE_MSB, 0x00);
    reg_write(REG_PREAMBLE_LSB, 0x08);

    // Sync word 0x12 (private LoRa network)
    reg_write(REG_SYNC_WORD, 0x12);

    // Detection optimise for SF7-12
    reg_write(REG_DETECTION_OPTIMIZE,  0xC3);
    reg_write(REG_DETECTION_THRESHOLD, 0x0A);

    // FIFO base addresses
    reg_write(REG_FIFO_TX_BASE_ADDR, 0x00);
    reg_write(REG_FIFO_RX_BASE_ADDR, 0x00);

    // DIO0 = RX_DONE
    reg_write(REG_DIO_MAPPING_1, 0x00);

    ESP_LOGI(TAG, "SX1276 configured: %s, SF7, BW125, +17dBm",
             freq == LORA_FREQ_868MHZ ? "868 MHz" : "915 MHz");
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t lora_manager_init(lora_freq_t freq)
{
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(spi_init(), TAG, "SPI init");
    ESP_RETURN_ON_ERROR(sx1276_reset(), TAG, "reset");
    ESP_RETURN_ON_ERROR(sx1276_configure(freq), TAG, "configure");

    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.enabled = true;

    ESP_LOGI(TAG, "LoRa manager initialised");
    return ESP_OK;
}

esp_err_t lora_manager_deinit(void)
{
    g_running = false;
    if (g_spi) { spi_bus_remove_device(g_spi); g_spi = NULL; }
    if (g_mutex) { vSemaphoreDelete(g_mutex); g_mutex = NULL; }
    return ESP_OK;
}

esp_err_t lora_manager_start(void)
{
    if (!g_spi) return ESP_ERR_INVALID_STATE;
    // Enter continuous RX mode waiting for packets
    reg_write(REG_FIFO_ADDR_PTR, 0x00);
    set_mode(MODE_RX_CONT);
    g_running = true;
    return ESP_OK;
}

esp_err_t lora_manager_stop(void)
{
    g_running = false;
    set_mode(MODE_STDBY);
    return ESP_OK;
}

esp_err_t lora_manager_send_position(double lat, double lon,
                                     int32_t alt_cm, uint32_t ts)
{
    if (!g_running || !g_spi) return ESP_ERR_INVALID_STATE;

    char buf[PROTOCOL_PACKET_MAX_LEN];
    int  len = snprintf(buf, sizeof(buf), "%s,%.6f,%.6f,%ld,%lu",
                        DEVICE_ID, lat, lon, (long)alt_cm, (unsigned long)ts);
    if (len <= 0 || len >= PROTOCOL_PACKET_MAX_LEN) return ESP_ERR_INVALID_SIZE;

    // Switch to standby, set FIFO, write payload, enter TX
    set_mode(MODE_STDBY);
    reg_write(REG_FIFO_ADDR_PTR, 0x00);
    reg_write(REG_PAYLOAD_LENGTH, (uint8_t)len);

    for (int i = 0; i < len; i++) {
        reg_write(REG_FIFO, (uint8_t)buf[i]);
    }

    set_mode(MODE_TX);

    // Wait for TX done (max 2 s)
    uint32_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
    while (!(reg_read(REG_IRQ_FLAGS) & IRQ_TX_DONE)) {
        if (xTaskGetTickCount() > deadline) {
            ESP_LOGE(TAG, "TX timeout");
            set_mode(MODE_RX_CONT);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    reg_write(REG_IRQ_FLAGS, IRQ_TX_DONE); // clear flag
    set_mode(MODE_RX_CONT);

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_stats.packets_tx++;
        xSemaphoreGive(g_mutex);
    }

    ESP_LOGD(TAG, "TX %d bytes", len);
    return ESP_OK;
}

bool lora_manager_receive(position_packet_t *out, int8_t *rssi_out)
{
    if (!g_running || !g_spi || !out) return false;

    uint8_t irq = reg_read(REG_IRQ_FLAGS);

    if (!(irq & IRQ_RX_DONE)) return false;

    // Clear all IRQ flags
    reg_write(REG_IRQ_FLAGS, 0xFF);

    if (irq & IRQ_CRC_ERROR) {
        ESP_LOGW(TAG, "CRC error");
        return false;
    }

    uint8_t nb_bytes  = reg_read(REG_RX_NB_BYTES);
    uint8_t curr_addr = reg_read(REG_FIFO_RX_CURR_ADDR);

    if (nb_bytes == 0 || nb_bytes >= PROTOCOL_PACKET_MAX_LEN) return false;

    reg_write(REG_FIFO_ADDR_PTR, curr_addr);

    char buf[PROTOCOL_PACKET_MAX_LEN];
    for (uint8_t i = 0; i < nb_bytes; i++) {
        buf[i] = (char)reg_read(REG_FIFO);
    }
    buf[nb_bytes] = '\0';

    int8_t rssi = (int8_t)(reg_read(REG_PKT_RSSI_VALUE) - 137);
    if (rssi_out) *rssi_out = rssi;

    // Parse position packet
    int n = sscanf(buf, "%15[^,],%lf,%lf,%ld,%lu",
                   out->device_id,
                   &out->latitude, &out->longitude,
                   (long *)&out->altitude_cm,
                   (unsigned long *)&out->timestamp);
    if (n != 5) {
        ESP_LOGW(TAG, "Bad LoRa packet");
        return false;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_stats.packets_rx++;
        g_stats.rssi_dbm      = rssi;
        g_stats.peer_in_range = true;
        g_stats.last_rx_tick  = xTaskGetTickCount();
        memcpy(&g_stats.peer_pos, out, sizeof(position_packet_t));
        xSemaphoreGive(g_mutex);
    }

    ESP_LOGD(TAG, "RX from %s RSSI=%d", out->device_id, rssi);
    return true;
}

void lora_manager_get_stats(protocol_stats_t *out)
{
    if (!out || !g_mutex) return;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        memcpy(out, &g_stats, sizeof(protocol_stats_t));
        uint32_t elapsed = (xTaskGetTickCount() - g_stats.last_rx_tick) * portTICK_PERIOD_MS;
        if (out->peer_in_range && elapsed > PROTOCOL_PEER_TIMEOUT_MS) {
            out->peer_in_range = false;
            g_stats.peer_in_range = false;
        }
        xSemaphoreGive(g_mutex);
    }
}
