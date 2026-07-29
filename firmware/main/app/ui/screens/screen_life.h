/*
 * screen_life.h — the primary life-total screen.
 *
 * Large centred life total starting at 40, wrapped by a 270° life ring that
 * shrinks and shifts green -> red as life falls. Dial or tap top/bottom to
 * change it, with a tactile "click" flash on every change.
 */
#ifndef LOTUS_SCREEN_LIFE_H
#define LOTUS_SCREEN_LIFE_H

#include "input_event.h"
#include "lvgl.h"

lv_obj_t *screen_life_create(void);
void screen_life_handle_input(input_event_t ev);

/** Reset life to the starting total (new game). */
void screen_life_reset(void);

/** Add `delta` to the life total (clamped). Used by the Commander screen to
 *  mirror commander damage into life. */
void screen_life_apply_delta(int delta);

/** Persistence access: read / write the absolute life total (no life-link). */
int screen_life_get_life(void);
void screen_life_set_life(int v);

/** Update the battery indicator: shows pct, or a filling animation while
 *  charging (one animation frame per call). A negative pct means the board has
 *  no battery to report, and hides the indicator entirely. */
void screen_life_set_battery(int pct, bool charging);

/** Redraw the ring of linked dials around the rim (Remote mode). Hidden while
 *  playing locally. Call from the network tick. */
void screen_life_refresh_peers(void);

#endif /* LOTUS_SCREEN_LIFE_H */
