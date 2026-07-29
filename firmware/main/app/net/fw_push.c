#include "fw_push.h"
#include "net_link.h"

#include "petal_hal.h"

#include <string.h>

#define FW_MAGIC  0x31574C46u  /* "FLW1" */
#define ACK_MS    400          /* how long to wait for a chunk to be acknowledged */
#define MAX_RETRY 8            /* consecutive resends before giving up on the link */

/* Payload per packet. The header plus this has to fit one datagram; fw_push_init
 * checks that against the platform's real MTU and disables the feature rather
 * than truncating anything. */
#define FW_CHUNK 236

enum {
    FW_MSG_OFFER = 1,   /* seq = total image bytes           */
    FW_MSG_READY = 2,   /* receiver opened its image slot    */
    FW_MSG_DATA  = 3,   /* seq = chunk index                 */
    FW_MSG_ACK   = 4,   /* seq = next chunk expected         */
    FW_MSG_END   = 5,
    FW_MSG_DONE  = 6,
    FW_MSG_ABORT = 7,
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  type;
    uint8_t  _pad[3];
    uint32_t seq;
    uint16_t len;
    uint8_t  data[FW_CHUNK];
} fw_pkt_t;

#define FW_HDR ((int)(sizeof(fw_pkt_t) - FW_CHUNK))

/* An inbound packet, handed over from the radio's context. */
typedef struct {
    uint8_t  mac[6];
    fw_pkt_t pkt;
    int      len;
} rx_item_t;

static volatile fw_state_t s_state;
static volatile int        s_pct;
static char                s_detail[48] = "";
static bool                s_busy;

static petal_queue_t *s_rx;      /* everything inbound                       */
static petal_queue_t *s_ctrl;    /* control replies, routed to the send task */

/* The peer we are talking to, and the offer awaiting the player's consent. */
static uint8_t  s_peer_mac[6];
static uint32_t s_offer_size;
static int64_t  s_offer_seen_us;   /* last time the sender asked */

fw_state_t  fw_push_state(void)   { return s_state; }
int         fw_push_percent(void) { return s_pct; }
const char *fw_push_detail(void)  { return s_detail; }

static void set_detail(const char *d)
{
    strncpy(s_detail, d ? d : "", sizeof(s_detail) - 1);
    s_detail[sizeof(s_detail) - 1] = '\0';
}

/* ---------- plumbing ---------- */

/* Runs in the radio's context (see net_link_set_raw_sink): filter and queue. */
static void raw_sink(const uint8_t *mac, const uint8_t *data, int len)
{
    if (!s_rx || len < FW_HDR || len > (int)sizeof(fw_pkt_t)) return;

    rx_item_t it;
    memset(&it, 0, sizeof(it));
    memcpy(&it.pkt, data, (size_t)len);
    if (it.pkt.magic != FW_MAGIC) return;
    memcpy(it.mac, mac, 6);
    it.len = len;
    petal_queue_send(s_rx, &it);
}

static void send_ctl(const uint8_t *mac, uint8_t type, uint32_t seq)
{
    fw_pkt_t p = { .magic = FW_MAGIC, .type = type, .seq = seq, .len = 0 };
    petal_radio_send(mac, &p, FW_HDR);
}

/* ---------- receiving ---------- */

static petal_ota_t *s_ota;
static uint32_t     s_expect;
static size_t       s_total;

static void recv_abort(const char *why)
{
    if (s_ota) { petal_ota_abort(s_ota); s_ota = NULL; }
    set_detail(why);
    s_state = FW_ERROR;
    s_busy  = false;
}

static void handle_rx(const rx_item_t *it)
{
    const fw_pkt_t *p = &it->pkt;

    switch (p->type) {
    case FW_MSG_OFFER: {
        if (s_busy && s_state == FW_SENDING) return;   /* we are the sender */
        if (s_state == FW_RECEIVING) return;           /* already taking one */

        /* Only from a dial we are actually linked with. Installing firmware is
         * otherwise unauthenticated: there is no image signature to check, so
         * pairing is the only thing standing between us and anything in range. */
        if (!net_link_is_member(it->mac)) {
            LV_LOG_WARN("fw: ignoring an offer from an unlinked device");
            return;
        }

        memcpy(s_peer_mac, it->mac, 6);
        s_offer_size    = p->seq;
        s_offer_seen_us = petal_now_us();
        s_state         = FW_OFFERED;      /* wait for the player to say yes */
        set_detail("from linked petal");
        break;
    }
    case FW_MSG_DATA: {
        if (s_state != FW_RECEIVING || !s_ota) return;
        if (memcmp(it->mac, s_peer_mac, 6) != 0) return;  /* not our sender */

        if (p->seq == s_expect) {
            if (!petal_ota_write(s_ota, p->data, p->len)) {
                recv_abort("write failed");
                send_ctl(it->mac, FW_MSG_ABORT, 0);
                return;
            }
            s_expect++;
            if (s_total) {
                s_pct = (int)(((uint64_t)s_expect * FW_CHUNK * 100) / s_total);
                if (s_pct > 100) s_pct = 100;
            }
        }
        /* Always acknowledge with what we want next, so a lost ACK self-heals:
         * the sender resends, we ask for the same chunk again, nothing resyncs. */
        send_ctl(it->mac, FW_MSG_ACK, s_expect);
        break;
    }
    case FW_MSG_END: {
        if (s_state != FW_RECEIVING || !s_ota) return;
        if (memcmp(it->mac, s_peer_mac, 6) != 0) return;

        s_state = FW_APPLYING;
        set_detail("installing");

        petal_ota_t *h = s_ota;
        s_ota = NULL;
        if (!petal_ota_finish(h)) {
            recv_abort("bad image");
            send_ctl(it->mac, FW_MSG_ABORT, 0);
            return;
        }

        s_pct   = 100;
        s_state = FW_OK;
        set_detail("rebooting");
        send_ctl(it->mac, FW_MSG_DONE, 0);
        petal_delay_ms(700);        /* let the packet leave, and the word be read */
        petal_reboot();
        break;
    }
    case FW_MSG_ABORT:
        if (s_state == FW_RECEIVING) recv_abort("sender aborted");
        break;
    default:
        /* Control replies belong to the send task. */
        if (s_ctrl) petal_queue_send(s_ctrl, it);
        break;
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    rx_item_t it;
    for (;;) {
        if (petal_queue_recv(s_rx, &it, -1)) handle_rx(&it);
    }
}

/* ---------- sending ---------- */

/* Wait for a specific control type, discarding anything stale. */
static bool await_ctl(uint8_t want, uint32_t *seq_out, int timeout_ms)
{
    int64_t deadline = petal_now_us() + (int64_t)timeout_ms * 1000;
    for (;;) {
        int64_t left_us = deadline - petal_now_us();
        if (left_us <= 0) return false;

        rx_item_t it;
        if (!petal_queue_recv(s_ctrl, &it, (int)(left_us / 1000) + 1)) return false;
        if (it.pkt.type == want) {
            if (seq_out) *seq_out = it.pkt.seq;
            return true;
        }
        if (it.pkt.type == FW_MSG_ABORT) return false;
    }
}

static void send_fail(const uint8_t *peer, const char *why)
{
    set_detail(why);
    s_state = FW_ERROR;
    s_busy  = false;
    if (peer) send_ctl(peer, FW_MSG_ABORT, 0);
}

static void send_task(void *arg)
{
    (void)arg;

    net_member_t others[NET_MAX_OTHERS];
    int n = net_link_others(others, NET_MAX_OTHERS);
    if (n < 1) {
        send_fail(NULL, "no petal linked");
        return;
    }
    uint8_t peer[6];
    memcpy(peer, others[0].mac, 6);

    size_t total = petal_ota_image_size();
    if (!total) {
        send_fail(NULL, "no image");
        return;
    }

    s_state = FW_SENDING;
    s_pct   = 0;

    /* The other dial asks its player to accept, so this waits on a human rather
     * than on a packet. Keep offering for a minute and say what we are waiting
     * for — a few short retries ran out long before anyone could reach the
     * button. */
    set_detail("waiting for accept");
    petal_queue_reset(s_ctrl);

    bool ready = false;
    for (int i = 0; i < 40 && !ready; i++) {
        send_ctl(peer, FW_MSG_OFFER, (uint32_t)total);
        ready = await_ctl(FW_MSG_READY, NULL, 1500);
    }
    if (!ready) {
        send_fail(NULL, "not accepted");
        return;
    }

    set_detail("sending");
    fw_pkt_t p = { .magic = FW_MAGIC, .type = FW_MSG_DATA };
    uint32_t chunks = (uint32_t)((total + FW_CHUNK - 1) / FW_CHUNK);

    for (uint32_t i = 0; i < chunks; i++) {
        size_t off = (size_t)i * FW_CHUNK;
        size_t len = total - off;
        if (len > FW_CHUNK) len = FW_CHUNK;

        if (!petal_ota_image_read(off, p.data, len)) {
            send_fail(peer, "read failed");
            return;
        }
        p.seq = i;
        p.len = (uint16_t)len;

        /* Stop-and-wait: one chunk in flight at a time, so a lost packet costs a
         * single retry rather than a resync. */
        bool acked = false;
        for (int attempt = 0; attempt < MAX_RETRY && !acked; attempt++) {
            petal_radio_send(peer, &p, FW_HDR + (int)len);
            uint32_t want = 0;
            if (await_ctl(FW_MSG_ACK, &want, ACK_MS) && want > i) acked = true;
        }
        if (!acked) {
            send_fail(peer, "link lost");
            return;
        }
        s_pct = (int)(((uint64_t)(i + 1) * 100) / chunks);
    }

    send_ctl(peer, FW_MSG_END, 0);
    s_pct   = 100;
    s_state = FW_OK;
    set_detail("sent - petal rebooting");
    s_busy  = false;
}

/* ---------- api ---------- */

/* The sender re-offers every 1.5 s while it waits. Once that stops it has given
 * up, and leaving the prompt on screen for ever would be a lie — worse, the
 * stale state used to block any later offer from being noticed at all. */
void fw_push_tick(void)
{
    if (s_state != FW_OFFERED) return;
    if (petal_now_us() - s_offer_seen_us > (int64_t)6000 * 1000) {
        s_state = FW_IDLE;
        set_detail("");
    }
}

void fw_push_accept(void)
{
    if (s_state != FW_OFFERED) return;

    s_ota = petal_ota_begin();
    if (!s_ota) { recv_abort("no slot"); return; }

    s_total  = s_offer_size;
    s_expect = 0;
    s_pct    = 0;
    s_busy   = true;
    s_state  = FW_RECEIVING;
    set_detail("receiving");
    send_ctl(s_peer_mac, FW_MSG_READY, 0);
}

void fw_push_decline(void)
{
    if (s_state != FW_OFFERED) return;
    send_ctl(s_peer_mac, FW_MSG_ABORT, 0);
    s_state = FW_IDLE;
    set_detail("");
}

void fw_push_init(void)
{
    /* None of this works on a platform whose datagrams are smaller than our
     * packet. Staying uninitialised keeps fw_push_available() false, so the UI
     * simply never offers a transfer. */
    if (FW_HDR + FW_CHUNK > petal_radio_mtu()) {
        LV_LOG_WARN("fw: chunk larger than the radio MTU — transfer disabled");
        return;
    }

    if (!s_rx)   s_rx   = petal_queue_create(8, sizeof(rx_item_t));
    if (!s_ctrl) s_ctrl = petal_queue_create(8, sizeof(rx_item_t));
    if (!s_rx || !s_ctrl) return;

    net_link_set_raw_sink(raw_sink);
    petal_task_start(rx_task, NULL, "fwrx", 6144, 6);
}

bool fw_push_available(void)
{
    return s_rx && net_link_radio_up() && net_link_member_count() > 1 &&
           petal_ota_image_size() > 0;
}

void fw_push_start(void)
{
    if (s_busy) return;
    if (!fw_push_available()) {
        set_detail("no petal linked");
        s_state = FW_ERROR;
        return;
    }
    s_busy = true;
    if (!petal_task_start(send_task, NULL, "fwtx", 8192, 5)) {
        s_busy = false;
        set_detail("could not start");
        s_state = FW_ERROR;
    }
}
