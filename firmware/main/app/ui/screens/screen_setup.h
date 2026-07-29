/*
 * screen_setup.h — the opening "How many players?" screen.
 *
 * Shown as an overlay after choosing a Local game, and again after a New Game.
 * Offers 2..8 players; the dial pre-highlights an option and a tap on a number
 * chooses it, sets the player count (which sizes the commander-damage wedges to
 * match the table), and returns to the Life screen.
 *
 * Not shown for a Remote game: there the pod size is discovered from however many
 * dials are linked, so asking would be asking a question we already know.
 */
#ifndef LOTUS_SCREEN_SETUP_H
#define LOTUS_SCREEN_SETUP_H

/** Build the opening screen once (call after the game screens exist). */
void screen_setup_init(void);

/** Show the player-count picker as an overlay over the current screen. */
void screen_setup_open(void);

#endif /* LOTUS_SCREEN_SETUP_H */
