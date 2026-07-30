#include "net_link.h"

#include "petal_hal.h"

#include <stddef.h>
#include <string.h>

#define NET_MAGIC 0x314E544Cu /* "LTN1" — tells our packets from anyone else's */
#define NET_VER   4

/* Wire-format rule, so dials on different builds keep talking:
 *   - fields up to and including `alive` are FROZEN. Never reorder or remove
 *     them, only append after.
 *   - a receiver accepts any length from that core up to the full struct,
 *     zero-filling whatever the sender did not send. A shorter packet is an
 *     older dial; a longer one is a newer dial with fields we do not know.
 *   - life and who is alive therefore always cross versions. Round state
 *     (seating, turn, generation, monarch, initiative) is only taken from an
 *     exactly matching version, since its meaning has changed before and a
 *     mismatched reading would be worse than none.
 *
 * ver 4 added monarch and initiative to the round state, reusing two padding
 * bytes so the packet size did not change. A ver-3 dial therefore still shares
 * life with a ver-4 dial, but not turn order — which is the intended trade. */

#define HEARTBEAT_MS 700   /* re-send own state at least this often */
#define EXPIRE_MS    6000  /* drop a member silent for this long */
#define RX_QUEUE_LEN 16

typedef enum {
    NET_MSG_STATE = 1,
    NET_MSG_LEAVE = 2,
} net_msg_type_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  type;
    uint8_t  ver;
    uint16_t pin;
    uint8_t  mac[6];
    int16_t  life;
    uint8_t  alive;
    uint8_t  active_pos;
    uint8_t  players;
    uint8_t  roll;
    uint8_t  order;
    uint8_t  monarch;    /* turn position holding the monarchy, 0 = nobody */
    uint8_t  gen;
    uint8_t  initiative; /* turn position holding the initiative, 0 = nobody */
    uint8_t  _pad[2];
    uint32_t turn;
    uint32_t epoch;      /* round state is shared: highest epoch wins */
} net_pkt_t;

/* Everything the oldest supported build also sends. */
#define NET_CORE_LEN ((int)offsetof(net_pkt_t, active_pos))

typedef struct {
    uint8_t  mac[6];
    int      life;
    bool     alive;
    int      roll;
    int      order;
    uint32_t turn;
    int64_t  last_seen_us;
    bool     used;
} slot_t;

static bool     s_radio_up;
static bool     s_active;
static uint16_t s_pin;
static uint8_t  s_self_mac[6];

static slot_t   s_slot[NET_MAX_OTHERS];
static int      s_self_seat;
static int      s_members = 1;

/* Own published state + when it last went out. */
static int      s_life;
static bool     s_alive = true;
static int      s_roll;          /* our own d20 for the first-player roll-off */
static int      s_order;         /* our own claimed turn position */
static bool     s_state_valid;
static int64_t  s_last_tx_us;

/* Shared round state (see header): guarded by an epoch rather than ownership. */
static uint32_t s_turn = 1;
static int      s_active_pos;    /* 1..s_players; 0 until play starts */
static int      s_players = 2;   /* table size, dials + anyone without one */
static uint32_t s_epoch;
static uint8_t  s_gen;        /* bumped to start a fresh round */
static int      s_monarch;    /* turn position holding the monarchy, 0 = nobody */
static int      s_initiative; /* turn position holding the initiative, 0 = nobody */
static bool     s_new_round;  /* someone else did; clear our game */

static petal_queue_t   *s_rx_q;
static net_raw_sink_fn  s_raw_sink;

/* ---------- receive path (radio context) ---------- */

/* Runs wherever the radio driver calls us from: no UI, no table writes — just
 * sort the packet and hand it on. */
static void radio_rx(const uint8_t src_mac[6], const uint8_t *data, int len)
{
    /* Game packets carry our magic; anything else belongs to whoever registered
     * the raw sink (the firmware transfer). Length is a range, not a match, so a
     * dial on an older or newer build is still heard. */
    if (len >= NET_CORE_LEN && len <= (int)sizeof(net_pkt_t)) {
        net_pkt_t pkt;
        memset(&pkt, 0, sizeof(pkt));
        memcpy(&pkt, data, (size_t)len);
        if (pkt.magic == NET_MAGIC) {
            /* Dropped when the queue is full: the heartbeat re-syncs us. */
            if (s_rx_q) petal_queue_send(s_rx_q, &pkt);
            return;
        }
    }
    if (s_raw_sink) s_raw_sink(src_mac, data, len);
}

/* ---------- setup ---------- */

bool net_link_init(void)
{
    if (s_radio_up) return true;

    /* A packet we cannot send in one piece would need fragmenting, which this
     * protocol deliberately does not do. */
    if ((int)sizeof(net_pkt_t) > petal_radio_mtu()) {
        LV_LOG_WARN("net_link: state packet exceeds the radio MTU");
        return false;
    }

    if (!s_rx_q) s_rx_q = petal_queue_create(RX_QUEUE_LEN, sizeof(net_pkt_t));
    if (!s_rx_q) return false;

    if (!petal_radio_init()) return false;
    petal_radio_self_mac(s_self_mac);
    petal_radio_set_rx(radio_rx);

    s_radio_up = true;
    return true;
}

uint16_t net_link_new_pin(void)
{
    return (uint16_t)(1000 + (petal_random() % 9000));
}

void net_link_set_pin(uint16_t pin)
{
    memset(s_slot, 0, sizeof(s_slot));
    s_members   = 1;
    s_self_seat = 0;
    s_pin       = pin;
    s_active    = true;
    s_last_tx_us = 0; /* force an immediate announce */

    /* No first player, and no seating, until the roll-off decides one. */
    s_roll        = 0;
    s_order       = 0;
    s_epoch       = 0;
    s_gen         = 0;
    s_new_round   = false;
    s_active_pos  = 0;
    s_players     = 2;
    s_turn        = 1;
    s_monarch     = 0;
    s_initiative  = 0;
}

void net_link_set_raw_sink(net_raw_sink_fn fn) { s_raw_sink = fn; }
bool net_link_radio_up(void) { return s_radio_up; }

bool net_link_is_member(const uint8_t mac[6])
{
    if (!s_active || !mac) return false;
    for (int i = 0; i < NET_MAX_OTHERS; i++) {
        if (s_slot[i].used && memcmp(s_slot[i].mac, mac, 6) == 0) return true;
    }
    return false;
}

uint16_t net_link_pin(void)  { return s_pin; }
bool net_link_active(void)   { return s_active; }

static void send_pkt(uint8_t type)
{
    if (!s_radio_up || !s_active) return;
    net_pkt_t p = {
        .magic       = NET_MAGIC,
        .type        = type,
        .ver         = NET_VER,
        .pin         = s_pin,
        .life        = (int16_t)s_life,
        .alive       = s_alive ? 1 : 0,
        .active_pos  = (uint8_t)s_active_pos,
        .players     = (uint8_t)s_players,
        .roll        = (uint8_t)s_roll,
        .order       = (uint8_t)s_order,
        .turn        = s_turn,
        .epoch       = s_epoch,
        .gen         = s_gen,
        .monarch     = (uint8_t)s_monarch,
        .initiative  = (uint8_t)s_initiative,
    };
    memcpy(p.mac, s_self_mac, 6);
    petal_radio_broadcast(&p, (int)sizeof(p));
    s_last_tx_us = petal_now_us();
}

void net_link_leave(void)
{
    if (s_active) send_pkt(NET_MSG_LEAVE);
    s_active = false;
    s_pin    = 0;
    memset(s_slot, 0, sizeof(s_slot));
    s_members   = 1;
    s_self_seat = 0;
}

void net_link_shutdown(void)
{
    if (!s_radio_up) return;
    if (s_active) send_pkt(NET_MSG_LEAVE);   /* tell the table we are going */

    s_active = false;
    s_pin    = 0;
    petal_radio_set_rx(NULL);
    petal_radio_shutdown();

    memset(s_slot, 0, sizeof(s_slot));
    s_members   = 1;
    s_self_seat = 0;
    s_radio_up  = false;   /* a later net_link_init() rebuilds it from scratch */
}

void net_link_publish(int life, bool alive)
{
    bool changed = !s_state_valid || life != s_life || alive != s_alive;
    s_life  = life;
    s_alive = alive;
    s_state_valid = true;
    if (changed) send_pkt(NET_MSG_STATE);
}

void net_link_set_roll(int roll)
{
    if (roll == s_roll) return;
    s_roll = roll;
    send_pkt(NET_MSG_STATE);
}

int      net_link_my_roll(void)      { return s_roll; }

void net_link_set_order(int order)
{
    if (order == s_order) return;
    s_order = order;
    send_pkt(NET_MSG_STATE);
}

int      net_link_my_order(void)     { return s_order; }
int      net_link_active_pos(void)   { return s_active_pos; }
int      net_link_players(void)      { return s_players < 2 ? 2 : s_players; }
uint32_t net_link_turn(void)         { return s_turn; }
bool     net_link_round_valid(void)  { return s_active_pos > 0; }

int net_link_monarch(void)    { return s_monarch; }
int net_link_initiative(void) { return s_initiative; }

/* Table-wide, so these travel with the epoch exactly like the turn pointer: a
 * change supersedes everyone else's view rather than being merged with it. That
 * is what makes "exactly one holder" representable at all — with a per-dial flag
 * the table can disagree and nothing can decide who is right. */
void net_link_set_monarch(int pos)
{
    if (pos == s_monarch) return;
    s_monarch = pos;
    s_epoch++;
    send_pkt(NET_MSG_STATE);
}

void net_link_set_initiative(int pos)
{
    if (pos == s_initiative) return;
    s_initiative = pos;
    s_epoch++;
    send_pkt(NET_MSG_STATE);
}

void net_link_begin_new_round(void)
{
    s_gen++;
    s_monarch    = 0;   /* a new game starts with the crown unclaimed */
    s_initiative = 0;
    /* Play restarts from the first seat. Deliberately NOT active_pos 0, which
     * would throw the table back to the roll-off — the seats are already
     * settled and re-deciding them every round is just ceremony. */
    net_link_set_round(1, s_players, 1);
}

bool net_link_take_new_round(void)
{
    bool v = s_new_round;
    s_new_round = false;
    return v;
}

void net_link_set_round(int active_pos, int players, uint32_t turn)
{
    s_active_pos = active_pos;
    s_players    = players < 2 ? 2 : players;
    s_turn       = turn;
    s_epoch++;                 /* our version now supersedes everyone else's */
    send_pkt(NET_MSG_STATE);
}

/* ---------- member table ---------- */

static slot_t *find_slot(const uint8_t *mac)
{
    for (int i = 0; i < NET_MAX_OTHERS; i++) {
        if (s_slot[i].used && memcmp(s_slot[i].mac, mac, 6) == 0) return &s_slot[i];
    }
    return NULL;
}

static slot_t *claim_slot(const uint8_t *mac)
{
    for (int i = 0; i < NET_MAX_OTHERS; i++) {
        if (!s_slot[i].used) {
            memset(&s_slot[i], 0, sizeof(s_slot[i]));
            memcpy(s_slot[i].mac, mac, 6);
            s_slot[i].used  = true;
            s_slot[i].alive = true;
            return &s_slot[i];
        }
    }
    return NULL; /* table full — extra dials are ignored */
}

/* Seats are the rank of each address in ascending order over all members, so
 * every dial derives the same ordering independently. */
static void recompute_seats(void)
{
    int n = 1, below = 0;
    for (int i = 0; i < NET_MAX_OTHERS; i++) {
        if (!s_slot[i].used) continue;
        n++;
        if (memcmp(s_slot[i].mac, s_self_mac, 6) < 0) below++;
    }
    s_members   = n;
    s_self_seat = below;
}

static int seat_of(const slot_t *s)
{
    int rank = 0;
    if (memcmp(s_self_mac, s->mac, 6) < 0) rank++;
    for (int i = 0; i < NET_MAX_OTHERS; i++) {
        if (!s_slot[i].used || &s_slot[i] == s) continue;
        if (memcmp(s_slot[i].mac, s->mac, 6) < 0) rank++;
    }
    return rank;
}

void net_link_tick(void)
{
    if (!s_radio_up) return;

    /* 1. drain everything the radio handed us */
    net_pkt_t p;
    while (s_rx_q && petal_queue_recv(s_rx_q, &p, 0)) {
        if (!s_active || p.pin != s_pin) continue;              /* another table */
        if (memcmp(p.mac, s_self_mac, 6) == 0) continue;        /* our own echo */

        if (p.type == NET_MSG_LEAVE) {
            slot_t *s = find_slot(p.mac);
            if (s) s->used = false;
            recompute_seats();
            continue;
        }

        slot_t *s = find_slot(p.mac);
        if (!s) s = claim_slot(p.mac);
        if (!s) continue;
        /* Core fields cross versions. */
        s->life         = p.life;
        s->alive        = p.alive != 0;
        s->last_seen_us = petal_now_us();
        recompute_seats();

        if (p.ver != NET_VER) continue;   /* older build: life only */

        s->roll         = p.roll;
        s->order        = p.order;
        s->turn         = p.turn;

        /* Shared round state: adopt anything newer than ours. */
        if (p.epoch > s_epoch) {
            s_epoch       = p.epoch;
            s_active_pos = p.active_pos;
            s_players    = p.players ? p.players : s_players;
            if (p.gen != s_gen) { s_gen = p.gen; s_new_round = true; }
            s_turn        = p.turn;
            s_monarch     = p.monarch;
            s_initiative  = p.initiative;
        }
    }

    /* 2. drop anyone who has gone quiet (powered off / walked away) */
    int64_t t = petal_now_us();
    bool dropped = false;
    for (int i = 0; i < NET_MAX_OTHERS; i++) {
        if (!s_slot[i].used) continue;
        if (s_slot[i].last_seen_us &&
            (t - s_slot[i].last_seen_us) > (int64_t)EXPIRE_MS * 1000) {
            s_slot[i].used = false;
            dropped = true;
        }
    }
    if (dropped) recompute_seats();

    /* 3. heartbeat so late joiners and lossy links converge */
    if (s_active && (t - s_last_tx_us) > (int64_t)HEARTBEAT_MS * 1000) {
        send_pkt(NET_MSG_STATE);
    }
}

int net_link_member_count(void) { return s_active ? s_members : 1; }
int net_link_self_seat(void)    { return s_active ? s_self_seat : 0; }

int net_link_others(net_member_t *out, int max)
{
    if (!out || max <= 0 || !s_active) return 0;
    int n = 0;
    for (int i = 0; i < NET_MAX_OTHERS && n < max; i++) {
        if (!s_slot[i].used) continue;
        memcpy(out[n].mac, s_slot[i].mac, 6);
        out[n].seat  = seat_of(&s_slot[i]);
        out[n].life  = s_slot[i].life;
        out[n].alive = s_slot[i].alive;
        out[n].roll  = s_slot[i].roll;
        out[n].order = s_slot[i].order;
        out[n].turn  = s_slot[i].turn;
        n++;
    }
    /* seat order, so the ring layout is stable on every dial */
    for (int i = 1; i < n; i++) {
        net_member_t key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].seat > key.seat) { out[j + 1] = out[j]; j--; }
        out[j + 1] = key;
    }
    return n;
}
