/*
 * fw_push.h — copy this dial's firmware onto another dial.
 *
 * Updating a dial normally means a cable. This is the alternative: two dials
 * that are already talking to each other to run a game can carry a firmware
 * image over the same link, so you flash one over USB and it passes the image to
 * the rest. No network, no access point, no server — which also means it still
 * works on a guest network that isolates its clients, or at a table with no
 * WiFi at all.
 *
 * The protocol is deliberately plain stop-and-wait: every chunk is acknowledged
 * before the next is sent. That caps throughput at roughly one chunk per round
 * trip — still under a minute for a typical image — and it means a dropped
 * packet costs one retry rather than a resync.
 *
 * Receiving takes two things: the sender must be a dial you are currently
 * linked with, and you must accept the offer on the receiving dial. Without both
 * of those, anything in radio range could install its own firmware, because
 * there is no image signature to fall back on.
 */
#ifndef LOTUS_FW_PUSH_H
#define LOTUS_FW_PUSH_H

#include <stdbool.h>

typedef enum {
    FW_IDLE = 0,
    FW_OFFERED,    /* a linked petal wants to send us firmware; awaiting consent */
    FW_SENDING,
    FW_RECEIVING,
    FW_APPLYING,   /* image received; finalising and about to reboot */
    FW_OK,
    FW_ERROR,
} fw_state_t;

/** Register the ESP-NOW sink so incoming pushes are accepted. Call once at boot. */
void fw_push_init(void);

/** True when a peer is linked and could be sent to. */
bool fw_push_available(void);

/** Start sending our running image to the first linked peer. */
void fw_push_start(void);

/** Expire a pending offer the sender has given up on. Call from a timer. */
void fw_push_tick(void);

/** Accept or refuse a pending offer (FW_OFFERED). */
void fw_push_accept(void);
void fw_push_decline(void);

fw_state_t  fw_push_state(void);
int         fw_push_percent(void);
const char *fw_push_detail(void);

#endif /* LOTUS_FW_PUSH_H */
