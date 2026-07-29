/*
 * input_event.h — the semantic input events a screen can receive.
 *
 * Screens never see touch coordinates or dial pins; they only ever see these.
 * app_main.c produces them:
 *   - the dial            -> INCREMENT / DECREMENT
 *   - a horizontal swipe  -> NEXT_SCREEN / PREV_SCREEN
 * Taps are not events: they are handled where they land, by each screen's own
 * tap zones (see ui_make_tap_zone).
 *
 * SELECT_NEXT / SELECT_PREV / ACTION exist for hardware with more controls than
 * the reference board — a second encoder, or a knob with a push-button. On a
 * device that cannot produce them they are simply never emitted, and every
 * screen stays fully operable with dial and touch alone.
 */
#ifndef LOTUS_INPUT_EVENT_H
#define LOTUS_INPUT_EVENT_H

typedef enum {
    INPUT_EV_INCREMENT,   /**< Primary value up by one (dial clockwise). */
    INPUT_EV_DECREMENT,   /**< Primary value down by one (dial anticlockwise). */
    INPUT_EV_SELECT_NEXT, /**< Move the selection forward within a screen. */
    INPUT_EV_SELECT_PREV, /**< Move the selection backward within a screen. */
    INPUT_EV_ACTION,      /**< Press / toggle (a push-button, if the board has one). */
    INPUT_EV_NEXT_SCREEN, /**< Advance to the next screen (swipe left). */
    INPUT_EV_PREV_SCREEN, /**< Back to the previous screen (swipe right). */
} input_event_t;

#endif /* LOTUS_INPUT_EVENT_H */
