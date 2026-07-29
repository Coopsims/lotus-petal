/*
 * screen_manager.h — owns the set of screens and routes input to the active one.
 *
 * Screens are registered generically (name + create + input handler), so adding
 * a screen never touches the manager. Swipe navigation (next/prev) is driven by
 * app_main.c from the touch panel; every other event is forwarded to the currently
 * visible screen's handler.
 */
#ifndef LOTUS_SCREEN_MANAGER_H
#define LOTUS_SCREEN_MANAGER_H

#include "input_event.h"
#include "lvgl.h"

/** Build the screen's widget tree and return its root screen object. */
typedef lv_obj_t *(*screen_create_fn)(void);
/** Handle a semantic input event while this screen is active. */
typedef void (*screen_input_fn)(input_event_t ev);

/** Register a screen. Call before screen_manager_start(). */
void screen_manager_add(const char *name, screen_create_fn create, screen_input_fn handle_input);

/** Instantiate all registered screens and show the first one. */
void screen_manager_start(void);

/** Cycle to the next screen (slide left). */
void screen_manager_next(void);

/** Cycle to the previous screen (slide right). */
void screen_manager_prev(void);

/** Dispatch a semantic input event to the manager / active screen. */
void screen_manager_handle_input(input_event_t ev, void *user_data);

#endif /* LOTUS_SCREEN_MANAGER_H */
