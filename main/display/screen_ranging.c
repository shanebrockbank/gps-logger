#include "screen_ranging.h"
#include <stdio.h>

esp_err_t screen_ranging_render(ssd1306_handle_t *display,
                                const protocol_stats_t stats[],
                                float distance_m,
                                bool sd_logging)
{
    if (!display) return ESP_ERR_INVALID_ARG;

    ssd1306_clear(display);

    // Row 0: title + screen indicator
    ssd1306_draw_string(display, 0,   0, "RANGING");
    ssd1306_draw_string(display, 96,  0, "[3/3]");

    // Row 1: distance to peer
    char buf[26];
    bool any_in_range = false;
    float best_dist   = distance_m;
    for (int i = 0; i < PROTOCOL_COUNT; i++) {
        if (stats[i].peer_in_range) { any_in_range = true; break; }
    }

    if (any_in_range && best_dist >= 0.0f) {
        if (best_dist < 1000.0f) {
            snprintf(buf, sizeof(buf), "Dist: %.0fm", best_dist);
        } else {
            snprintf(buf, sizeof(buf), "Dist: %.2fkm", best_dist / 1000.0f);
        }
    } else {
        snprintf(buf, sizeof(buf), "No peer in range");
    }
    ssd1306_draw_string(display, 0, 10, buf);

    // Row 2: ESP-NOW
    if (stats[PROTOCOL_ESPNOW].enabled) {
        snprintf(buf, sizeof(buf), "NOW:%ddBm %s",
                 stats[PROTOCOL_ESPNOW].rssi_dbm,
                 stats[PROTOCOL_ESPNOW].peer_in_range ? "IN " : "OUT");
    } else {
        snprintf(buf, sizeof(buf), "NOW: --");
    }
    ssd1306_draw_string(display, 0, 20, buf);

    // Row 3: WiFi
    if (stats[PROTOCOL_WIFI].enabled) {
        snprintf(buf, sizeof(buf), "WiFi:%ddBm %dAP",
                 stats[PROTOCOL_WIFI].rssi_dbm,
                 stats[PROTOCOL_WIFI].packets_rx);
    } else {
        snprintf(buf, sizeof(buf), "WiFi: --");
    }
    ssd1306_draw_string(display, 0, 30, buf);

    // Row 4: BLE
    if (stats[PROTOCOL_BLE].enabled) {
        snprintf(buf, sizeof(buf), "BLE: %d dev",
                 stats[PROTOCOL_BLE].packets_rx);
    } else {
        snprintf(buf, sizeof(buf), "BLE:  --");
    }
    ssd1306_draw_string(display, 0, 40, buf);

    // Row 5: LoRa + SD indicator
    if (stats[PROTOCOL_LORA].enabled) {
        snprintf(buf, sizeof(buf), "LoRa:%ddBm %s%s",
                 stats[PROTOCOL_LORA].rssi_dbm,
                 stats[PROTOCOL_LORA].peer_in_range ? "IN " : "OUT",
                 sd_logging ? " SD" : "");
    } else {
        snprintf(buf, sizeof(buf), "LoRa: --%s", sd_logging ? " SD" : "");
    }
    ssd1306_draw_string(display, 0, 54, buf);

    return ESP_OK;
}
