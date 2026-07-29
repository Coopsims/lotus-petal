/*
 * screen_counters.h — data-driven list of secondary counters (poison, monarch,
 * day/night, ...). Add a counter by dropping another counter_t in the table in
 * the .c; the row widgets and input routing adapt automatically. Tap a row's
 * upper/lower half to step it (also selects it); the dial adjusts the selected.
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
