/*
 * screen_counters.h — data-driven grid of secondary counters (tokens, poison,
 * commander tax, monarch, the Ring, day/night, ...).
 *
 * Add a counter by dropping another counter_t in the table in the .c; the tiles,
 * the input routing and the persistence all adapt. Anything special about it goes
 * in its `role` / `per_turn` fields — never key behaviour off the display name.
 *
 * Tap a tile to select it; the dial adjusts the selected one. Taps never change a
 * value, so an accidental tap while swiping cannot alter a count.
 */
#ifndef LOTUS_SCREEN_COUNTERS_H
#define LOTUS_SCREEN_COUNTERS_H

#include "input_event.h"
#include "lvgl.h"

lv_obj_t *screen_counters_create(void);
void screen_counters_handle_input(input_event_t ev);

/** Reset every counter to its starting value (new game). */
void screen_counters_reset(void);

/** Persistence access: number of counters, and get/set one by index. */
int screen_counters_num(void);

/** Current poison count (0 if none) — shown on the life screen. */
int screen_counters_poison(void);
int screen_counters_get_value(int i);
void screen_counters_set_value(int i, int v);

/** Advance a turn: zero the per-turn counters (e.g. Storm). */
void screen_counters_new_turn(void);

#endif /* LOTUS_SCREEN_COUNTERS_H */
