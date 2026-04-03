/**
 * @file protocol_types.h
 * @brief Shared data structures for all wireless protocol managers
 *
 * Defines the position packet format (identical across all protocols)
 * and per-protocol statistics structure used by the display and SD logger.
 *
 * Wire format:  "<device_id>,<lat>,<lon>,<alt_cm>,<timestamp>"
 * Example:      "DEV1,37.774900,-122.419400,30,1699564821"
 */

#ifndef PROTOCOL_TYPES_H
#define PROTOCOL_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/** Maximum length of device identifier string */
#define PROTOCOL_DEVICE_ID_LEN   16

/** Maximum length of serialised position packet string */
#define PROTOCOL_PACKET_MAX_LEN  80

/** Peer timeout — no packet for this many ms → out of range */
#define PROTOCOL_PEER_TIMEOUT_MS 5000

// ── Position packet ───────────────────────────────────────────────────────────

/**
 * Position packet transmitted by every wireless protocol.
 * All fields are filled by the GPS manager before each transmission.
 */
typedef struct {
    char     device_id[PROTOCOL_DEVICE_ID_LEN]; // Unique device identifier
    double   latitude;                           // Decimal degrees, positive = N
    double   longitude;                          // Decimal degrees, positive = E
    int32_t  altitude_cm;                        // Altitude in centimetres
    uint32_t timestamp;                          // Unix epoch seconds (or uptime)
} position_packet_t;

// ── Per-protocol statistics ───────────────────────────────────────────────────

/**
 * Runtime statistics for one wireless protocol.
 * Updated by the protocol manager, read by screen_ranging and sd_logger.
 */
typedef struct {
    bool     enabled;              // Protocol initialised and running
    bool     peer_in_range;        // True while packets arriving within timeout
    int8_t   rssi_dbm;            // Last received signal strength (dBm)
    uint32_t packets_tx;          // Total packets transmitted
    uint32_t packets_rx;          // Total packets received
    float    packet_loss_pct;     // Rolling loss percentage (0–100)
    float    distance_m;          // Haversine distance to last known peer position
    uint32_t last_rx_tick;        // xTaskGetTickCount() of last received packet
    position_packet_t peer_pos;   // Most recent peer position received
} protocol_stats_t;

// ── Aggregated view across all protocols ─────────────────────────────────────

typedef enum {
    PROTOCOL_ESPNOW = 0,
    PROTOCOL_WIFI,
    PROTOCOL_BLE,
    PROTOCOL_LORA,
    PROTOCOL_COUNT
} protocol_id_t;

/** Human-readable protocol names — indexed by protocol_id_t */
static const char *const PROTOCOL_NAMES[PROTOCOL_COUNT] = {
    "ESP-NOW",
    "WiFi",
    "BLE",
    "LoRa",
};

#endif // PROTOCOL_TYPES_H
