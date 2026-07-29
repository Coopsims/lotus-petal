/*
 * net_link.h — hostless dial-to-dial link.
 *
 * Model (deliberately hostless — there is no master):
 *   - A game is identified by a 4-digit PIN. One dial generates a random PIN
 *     ("create"), the others are told it ("join"). That is the ONLY difference
 *     between the two: both then behave identically.
 *   - Every dial broadcasts its own state (life, turn, alive) a few times a
 *     second. A dial is the single writer of its own seat, so there are no write
 *     conflicts and no locking — a missed packet is corrected by the next
 *     heartbeat.
 *   - Membership is discovered, not configured: anyone broadcasting the same PIN
 *     is a member. Seats are assigned by sorting every member's hardware
 *     address, so all devices independently compute the SAME seat order with no
 *     negotiation, and the table auto-resizes as dials join or drop.
 *   - The turn counter is shared by simply adopting the highest turn number seen.
 *     With a deterministic seat order that also gives a shared "active seat"
 *     ((turn - 1) % members) for free, with no extra protocol.
 *
 * Transport: nothing here knows what radio it is on. It needs only what
 * petal_hal.h offers — broadcast a datagram, send one to an address, be told
 * about arrivals — and assumes packets can be lost, duplicated or reordered.
 * ESP-NOW provides it on the reference board.
 *
 * Threading: the radio's receive hook runs in the driver's own context, so it
 * only pushes raw packets onto a queue. net_link_tick() drains that queue from
 * the LVGL timer, so the member table is only ever touched from one thread and
 * the UI can read it without locking.
 */
#ifndef LOTUS_NET_LINK_H
#define LOTUS_NET_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* Up to 8 seats at a table => 7 remote members plus this device. */
#define NET_MAX_MEMBERS 8
#define NET_MAX_OTHERS  (NET_MAX_MEMBERS - 1)

typedef struct {
    uint8_t  mac[6];
    int      seat;       /* 0-based, by address sort (identical on every dial) */
    int      life;
    bool     alive;
    int      roll;       /* d20 roll-off for first player; 0 = has not rolled */
    int      order;      /* claimed turn position, 1 = first; 0 = unclaimed  */
    uint32_t turn;
} net_member_t;

/** Bring the radio up. Safe to call more than once; does nothing after the first
 *  success. Only called when the player picks a linked game, so a local game
 *  never powers the radio. Returns false if the radio failed to start. */
bool net_link_init(void);

/** A fresh random PIN in 1000..9999 (for "create"). */
uint16_t net_link_new_pin(void);

/** Join the game identified by `pin` and start broadcasting. */
void net_link_set_pin(uint16_t pin);

/** The PIN currently in use (0 when not in a remote game). */
uint16_t net_link_pin(void);

/** True once a PIN is set and we are broadcasting. */
bool net_link_active(void);

/** Announce our departure and stop broadcasting. */
void net_link_leave(void);

/** Release the radio completely, so something else could use it. A later
 *  net_link_init() brings it back up from scratch. */
void net_link_shutdown(void);

/** Publish this device's own seat state. Sent immediately when it changes and
 *  re-sent periodically as a heartbeat, so peers converge after any packet loss. */
void net_link_publish(int life, bool alive);

/* ---- shared round state ----
 * Unlike life (which each dial owns), the turn pointer is a single value shared
 * by the table, so it carries an epoch: every change bumps it, and a dial adopts
 * any state with a higher epoch. Whoever holds the turn is the only one who
 * passes it, so in practice there is nothing to race over. */

/** Turn position being played right now, 1..players. 0 before the game starts.
 *  A position, not a seat: some seats at the table may have no dial at all. */
int net_link_active_pos(void);

/** Total players at the table, including anyone without a dial. */
int net_link_players(void);

/** Shared turn number (a full lap of the table). */
uint32_t net_link_turn(void);

/** True once play has started (a first player was rolled). */
bool net_link_round_valid(void);

/** Start a fresh round across the table, keeping everyone linked. Bumps a
 *  generation counter the other dials notice and clear their own game on. */
void net_link_begin_new_round(void);

/** True once, when another dial has started a new round — the caller should
 *  clear its own game state in response. */
bool net_link_take_new_round(void);

/** Set and broadcast the shared round state (bumps the epoch).
 *  `active_pos` 0 means "not started yet", which is how the table size can be
 *  agreed before anyone has rolled. */
void net_link_set_round(int active_pos, int players, uint32_t turn);

/** This dial's d20 roll for turn order (0 clears it, e.g. to re-roll a tie).
 *  Each dial owns its own roll, so no epoch is needed — every dial sees every
 *  roll and independently derives the same turn order. */
void net_link_set_roll(int roll);
int  net_link_my_roll(void);

/** This dial's claimed turn position (1 = first). Also single-writer: each dial
 *  claims its own, and a clash is settled locally by a fixed rule rather than by
 *  asking anyone. 0 clears the claim. */
void net_link_set_order(int order);
int  net_link_my_order(void);

/** The radio offers a single receive hook, so anything that is not a game packet
 *  is handed to this sink instead (which is how the firmware transfer hears its
 *  own traffic). Runs in the radio's context — queue the data and return; do not
 *  touch the UI. */
typedef void (*net_raw_sink_fn)(const uint8_t *src_mac, const uint8_t *data, int len);
void net_link_set_raw_sink(net_raw_sink_fn fn);

/** Radio up? The firmware transfer needs the radio running, but not a game. */
bool net_link_radio_up(void);

/** True if `mac` is a dial currently linked to us on this PIN. The firmware
 *  transfer uses this so it will only take an image from a dial you have paired
 *  with, rather than from anything in radio range. */
bool net_link_is_member(const uint8_t mac[6]);

/** Drain received packets, expire silent members, send the heartbeat.
 *  Call from an LVGL timer (~4 Hz). */
void net_link_tick(void);

/** Number of dials in the game INCLUDING this one (>= 1). */
int net_link_member_count(void);

/** This device's seat index (0-based), or 0 when not in a remote game. */
int net_link_self_seat(void);

/** Copy the other members (seat-ordered) into `out`. Returns how many were written. */
int net_link_others(net_member_t *out, int max);

#endif /* LOTUS_NET_LINK_H */
