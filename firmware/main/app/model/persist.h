/*
 * persist.h — save/restore the in-progress game across power cycles.
 *
 * The full game state (life, turn, player count, commander damage, and every
 * token/counter — which includes commander tax) is snapshotted to NVS. On boot
 * app_main.c asks whether a saved game exists and offers Resume vs New Game.
 *
 * Saving is dirty-flag driven: mutations call persist_mark_dirty(), and a timer
 * calls persist_flush() a couple of times a second, which writes only when dirty
 * (NVS also skips physically re-writing identical blobs, so wear stays low).
 */
#ifndef LOTUS_PERSIST_H
#define LOTUS_PERSIST_H

#include <stdbool.h>

/** Load any saved snapshot from NVS into memory. Call once at boot after the
 *  screens are built (does not touch the UI). */
void persist_init(void);

/** True if persist_init() found a valid saved game. */
bool persist_has_saved_game(void);

/** Read-outs of the saved snapshot for the Resume prompt (0 if none). */
int persist_saved_turn(void);
int persist_saved_life(void);
int persist_saved_players(void);

/** Apply the loaded snapshot to the live screens (Resume). */
void persist_resume(void);

/** Mark the game state changed so the next flush writes it. */
void persist_mark_dirty(void);

/** Write the current game state to NVS if it has changed since the last write. */
void persist_flush(void);

#endif /* LOTUS_PERSIST_H */
