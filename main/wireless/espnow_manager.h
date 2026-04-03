#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include "protocol_types.h"
#include "esp_err.h"

esp_err_t espnow_manager_init(void);
esp_err_t espnow_manager_deinit(void);
esp_err_t espnow_manager_start(void);
esp_err_t espnow_manager_stop(void);

/** Send our current GPS position to all peers (broadcast). */
esp_err_t espnow_manager_broadcast_position(double lat, double lon,
                                             int32_t alt_cm, uint32_t ts);

/** Get snapshot of current ESP-NOW statistics (thread-safe). */
void espnow_manager_get_stats(protocol_stats_t *out);

#endif // ESPNOW_MANAGER_H
