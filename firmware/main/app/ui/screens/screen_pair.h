/*
 * screen_pair.h — Remote-play pairing: create a game (shows a random 4-digit
 * PIN) or join one (dial the PIN in). Both paths end at the same "linked" view,
 * because the link is hostless — the PIN is just the group id, and creating vs
 * joining differ only in who picked the number.
 */
#ifndef LOTUS_SCREEN_PAIR_H
#define LOTUS_SCREEN_PAIR_H

void screen_pair_init(void);
void screen_pair_open(void);

/** Link dials without starting a game — used from Settings so firmware can be
 *  pushed between petals without setting a table up first. */
void screen_pair_open_link(void);

/** Refresh the linked-dial count. Call from the network tick. */
void screen_pair_tick(void);

#endif /* LOTUS_SCREEN_PAIR_H */
