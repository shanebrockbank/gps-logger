#ifndef SCREEN_RANGING_H
#define SCREEN_RANGING_H

#include "ssd1306_driver.h"
#include "wireless/protocol_types.h"
#include "esp_err.h"

/**
 * @brief Render the ranging screen (protocol stats + distance).
 *
 * Layout (128×64):
 *   Line 0:  "RANGING" + "[3/3]"
 *   Line 1:  Distance or "No Peer"
 *   Line 2:  ESP-NOW RSSI + in/out
 *   Line 3:  WiFi APs seen
 *   Line 4:  BLE devices seen
 *   Line 5:  LoRa RSSI + in/out
 */
esp_err_t screen_ranging_render(ssd1306_handle_t *display,
                                const protocol_stats_t stats[],
                                float distance_m,
                                bool sd_logging);

#endif // SCREEN_RANGING_H
