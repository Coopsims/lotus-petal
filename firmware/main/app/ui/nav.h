/*
 * nav.h — a tiny stack of overlay screens.
 *
 * The swipe screens (Life and Counters) are the base layer, owned by the screen
 * manager. Everything else — commander damage, the Tools pie, Settings, dice,
 * pairing — is pushed on top of them as an overlay, which is why none of it can
 * be reached by an accidental swipe.
 *
 * While any overlay is open, app_main.c stops feeding the dial and swipes to the
 * game and routes them here instead: the dial goes to the top overlay's handler,
 * and a right-swipe pops one level.
 */
#ifndef LOTUS_NAV_H
#define LOTUS_NAV_H

#include <stdbool.h>
#include "lvgl.h"

/** Per-overlay dial handler: +/- detents while that overlay is on top. */
typedef void (*nav_enc_fn)(int delta);

/** Push `scr` as a new overlay (remembering the current screen to return to)
 *  and load it. `enc` may be NULL if the overlay ignores the dial. */
void nav_push(lv_obj_t *scr, nav_enc_fn enc);

/** Pop the top overlay, returning to the screen beneath it. */
void nav_pop(void);

/** Pop every overlay, returning all the way to the base game screen. */
void nav_pop_all(void);

/** True while at least one overlay is open. */
bool nav_active(void);

/** Route a dial delta to the top overlay's encoder handler (if any). */
void nav_encoder(int delta);

#endif /* LOTUS_NAV_H */
