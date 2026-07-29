/*
 * hal_radio.c — connectionless datagrams between dials, over ESP-NOW.
 *
 * ESP-NOW is a good fit for a table of dials: no access point, no association,
 * no DHCP, no lwip — just addressed or broadcast 802.11 frames up to 250 bytes.
 * That is why the link protocol above can be hostless.
 *
 * Two details a replacement transport has to reproduce:
 *   - A fixed channel. Broadcast frames only reach peers listening on the same
 *     channel, and an associated station follows its access point's channel
 *     instead, so any association is dropped here on purpose.
 *   - No modem sleep. A sleeping radio misses the other dials' broadcasts.
 */
#include "petal_hal.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"

#include <string.h>

static const char *TAG = "hal.radio";

/* Every dial has to agree on this for broadcast to reach anyone. */
#define RADIO_CHANNEL 1

static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool              s_up;
static petal_radio_rx_fn s_rx;

int petal_radio_mtu(void)
{
    return ESP_NOW_MAX_DATA_LEN;   /* 250 */
}

/* Runs in the WiFi task. Hands the bytes straight up; the app layer is
 * responsible for not doing anything slow here. */
static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (s_rx && info) s_rx(info->src_addr, data, len);
}

bool petal_radio_init(void)
{
    if (s_up) return true;

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "event loop: %s", esp_err_to_name(err));
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGW(TAG, "wifi init: %s", esp_err_to_name(err));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);   /* nothing worth persisting */
    esp_wifi_set_mode(WIFI_MODE_STA);
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi start: %s", esp_err_to_name(err));
        return false;
    }

    /* Drop any association: while associated the radio follows the access
     * point's channel, and our fixed-channel broadcasts would never land. */
    esp_wifi_disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        ESP_LOGW(TAG, "esp-now init failed");
        return false;
    }
    esp_now_register_recv_cb(recv_cb);

    /* One broadcast peer covers the whole table, so peers never have to be added
     * or removed as dials come and go. */
    if (!esp_now_is_peer_exist(BROADCAST)) {
        esp_now_peer_info_t peer = {0};
        memcpy(peer.peer_addr, BROADCAST, 6);
        peer.channel = RADIO_CHANNEL;
        peer.ifidx   = WIFI_IF_STA;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    s_up = true;
    ESP_LOGI(TAG, "esp-now up on channel %d", RADIO_CHANNEL);
    return true;
}

void petal_radio_shutdown(void)
{
    if (!s_up) return;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    s_up = false;
}

bool petal_radio_is_up(void)
{
    return s_up;
}

void petal_radio_self_mac(uint8_t out[6])
{
    if (!out) return;
    /* From eFuse, so it answers whether or not the radio has been started — the
     * protocol needs this identity before it powers anything up. */
    if (esp_read_mac(out, ESP_MAC_WIFI_STA) != ESP_OK) memset(out, 0, 6);
}

void petal_radio_set_rx(petal_radio_rx_fn fn)
{
    s_rx = fn;
}

bool petal_radio_broadcast(const void *data, int len)
{
    if (!s_up) return false;
    return esp_now_send(BROADCAST, (const uint8_t *)data, (size_t)len) == ESP_OK;
}

/* Add the peer on demand so callers never manage a peer table. Channel 0 means
 * "whatever channel we are already on". */
static bool ensure_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool petal_radio_send(const uint8_t mac[6], const void *data, int len)
{
    if (!s_up) return false;
    ensure_peer(mac);
    return esp_now_send(mac, (const uint8_t *)data, (size_t)len) == ESP_OK;
}
