#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include "protocol_types.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/** Supported centre frequencies */
typedef enum {
    LORA_FREQ_868MHZ,   // EU ISM band
    LORA_FREQ_915MHZ,   // US ISM band
} lora_freq_t;

esp_err_t lora_manager_init(lora_freq_t freq);
esp_err_t lora_manager_deinit(void);
esp_err_t lora_manager_start(void);
esp_err_t lora_manager_stop(void);

/** Transmit a position packet (non-blocking after TX complete). */
esp_err_t lora_manager_send_position(double lat, double lon,
                                     int32_t alt_cm, uint32_t ts);

/** Poll for a received packet (call from ranging task, non-blocking). */
bool      lora_manager_receive(position_packet_t *out, int8_t *rssi_out);

/** Get current protocol stats snapshot. */
void      lora_manager_get_stats(protocol_stats_t *out);

#endif // LORA_MANAGER_H
