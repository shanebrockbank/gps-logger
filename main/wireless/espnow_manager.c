#include "espnow_manager.h"
#include "gps/distance_calc.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ESPNOW";

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const char    DEVICE_ID[]      = "DEV1";

static protocol_stats_t  g_stats   = {0};
static SemaphoreHandle_t g_mutex   = NULL;
static bool              g_running = false;

// ── WiFi init for ESP-NOW ─────────────────────────────────────────────────────

static esp_err_t wifi_init_for_espnow(void)
{
    esp_netif_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE), TAG, "wifi ch");
    return ESP_OK;
}

// ── Packet serialisation ──────────────────────────────────────────────────────

static int pack_position(char *buf, size_t buf_len,
                         const char *id, double lat, double lon,
                         int32_t alt_cm, uint32_t ts)
{
    return snprintf(buf, buf_len, "%s,%.6f,%.6f,%ld,%lu",
                    id, lat, lon, (long)alt_cm, (unsigned long)ts);
}

static bool unpack_position(const char *buf, position_packet_t *out)
{
    int n = sscanf(buf, "%15[^,],%lf,%lf,%ld,%lu",
                   out->device_id,
                   &out->latitude,
                   &out->longitude,
                   (long *)&out->altitude_cm,
                   (unsigned long *)&out->timestamp);
    return (n == 5);
}

// ── ESP-NOW callbacks ─────────────────────────────────────────────────────────

static void on_data_sent(const uint8_t *mac, esp_now_send_status_t status)
{
    if (xSemaphoreTake(g_mutex, 0) == pdTRUE) {
        if (status == ESP_NOW_SEND_SUCCESS) g_stats.packets_tx++;
        xSemaphoreGive(g_mutex);
    }
}

static void on_data_recv(const esp_now_recv_info_t *info,
                         const uint8_t *data, int len)
{
    if (!g_running || len <= 0 || len >= PROTOCOL_PACKET_MAX_LEN) return;

    char buf[PROTOCOL_PACKET_MAX_LEN];
    memcpy(buf, data, len);
    buf[len] = '\0';

    position_packet_t pkt = {0};
    if (!unpack_position(buf, &pkt)) {
        ESP_LOGW(TAG, "Bad packet: %s", buf);
        return;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    g_stats.packets_rx++;
    g_stats.rssi_dbm = info->rx_ctrl->rssi;
    g_stats.last_rx_tick = xTaskGetTickCount();
    g_stats.peer_in_range = true;
    memcpy(&g_stats.peer_pos, &pkt, sizeof(position_packet_t));

    // Rolling packet loss (simple approximation)
    if (g_stats.packets_tx > 0) {
        g_stats.packet_loss_pct = 100.0f * (1.0f - (float)g_stats.packets_rx /
                                              (float)g_stats.packets_tx);
        if (g_stats.packet_loss_pct < 0.0f) g_stats.packet_loss_pct = 0.0f;
    }

    xSemaphoreGive(g_mutex);

    ESP_LOGD(TAG, "RX from %s RSSI=%d", pkt.device_id, info->rx_ctrl->rssi);
}

// ── Timeout watchdog (called from espnow_manager_get_stats) ──────────────────

static void check_peer_timeout(protocol_stats_t *s)
{
    uint32_t elapsed = (xTaskGetTickCount() - s->last_rx_tick) * portTICK_PERIOD_MS;
    if (s->peer_in_range && elapsed > PROTOCOL_PEER_TIMEOUT_MS) {
        s->peer_in_range = false;
        ESP_LOGI(TAG, "Peer out of range (%ums silence)", (unsigned)elapsed);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t espnow_manager_init(void)
{
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(wifi_init_for_espnow(), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init");

    esp_now_register_send_cb(on_data_sent);
    esp_now_register_recv_cb(on_data_recv);

    // Add broadcast peer
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = 0;
    peer.ifidx   = ESP_IF_WIFI_STA;
    peer.encrypt = false;
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&peer), TAG, "add peer");

    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.enabled = true;

    ESP_LOGI(TAG, "ESP-NOW initialised");
    return ESP_OK;
}

esp_err_t espnow_manager_deinit(void)
{
    g_running = false;
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    if (g_mutex) { vSemaphoreDelete(g_mutex); g_mutex = NULL; }
    return ESP_OK;
}

esp_err_t espnow_manager_start(void) { g_running = true;  return ESP_OK; }
esp_err_t espnow_manager_stop(void)  { g_running = false; return ESP_OK; }

esp_err_t espnow_manager_broadcast_position(double lat, double lon,
                                             int32_t alt_cm, uint32_t ts)
{
    if (!g_running) return ESP_ERR_INVALID_STATE;

    char buf[PROTOCOL_PACKET_MAX_LEN];
    int  len = pack_position(buf, sizeof(buf), DEVICE_ID, lat, lon, alt_cm, ts);
    if (len <= 0) return ESP_FAIL;

    esp_err_t ret = esp_now_send(BROADCAST_MAC, (uint8_t *)buf, (size_t)len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

void espnow_manager_get_stats(protocol_stats_t *out)
{
    if (!out || !g_mutex) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    memcpy(out, &g_stats, sizeof(protocol_stats_t));
    check_peer_timeout(out);
    // Write back updated in_range flag
    g_stats.peer_in_range = out->peer_in_range;
    xSemaphoreGive(g_mutex);

    // Update distance if we have a peer position
    if (out->peer_in_range &&
        out->peer_pos.latitude != 0.0 && out->peer_pos.longitude != 0.0) {
        // Caller should supply our own position; use 0,0 as placeholder here
        // distance updated by ranging_task in main.c
    }
}
