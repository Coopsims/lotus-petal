/*
 * screen_commander.h — commander-damage tracker, reached by a short tap on the
 * Life screen (an overlay, not part of the swipe order).
 *
 * One wedge per seat at the table, ours included — a commander can damage its own
 * controller, and that has to be recordable somewhere. Each wedge runs 0..21 and
 * turns red at 21, around a centre readout of the selected seat. Tap a wedge to
 * select it, dial to adjust it, swipe right to go back.
 *
 * Damage is LINKED to life: dialling a commander's damage up takes your life down
 * by the same amount, and dialling it back restores it. Pushing against the 0 or
 * 21 clamp does not move life, so you cannot bleed life into a limit.
 */
#ifndef LOTUS_SCREEN_COMMANDER_H
#define LOTUS_SCREEN_COMMANDER_H

#include "lvgl.h"

/** Commander damage at or above this from a single commander is lethal. Used as
 *  the per-wedge ceiling and by the elimination check, so the rule lives once. */
#define COMMANDER_LETHAL 21

/** Build the Commander overlay once (call after the Life screen exists). */
void screen_commander_init(void);

/** Show the Commander overlay over the current screen. */
void screen_commander_open(void);

/** Lay the wedges out for a table of `n` seats, ours included (clamped 2..8), and
 *  zero every damage total. */
void screen_commander_set_opponents(int n);

/** Reset every commander-damage total to 0 (new game). */
void screen_commander_reset(void);

/** Persistence access: how many wedges exist, and per-wedge damage get/set. `set`
 *  does NOT touch life — it is for restoring a saved game, where the life total
 *  is restored separately and would otherwise be double-counted. */
int screen_commander_opponent_count(void);
int screen_commander_get_damage(int i);
void screen_commander_set_damage(int i, int v);

/** Highest damage taken from any single commander — a lethal threshold at 21. */
int screen_commander_max_damage(void);

#endif /* LOTUS_SCREEN_COMMANDER_H */
