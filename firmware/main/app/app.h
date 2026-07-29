/*
 * app.h — the application's entry point, and the one thing the screens need from
 * it that is not a screen.
 *
 * The platform calls app_run() once the hardware is up. Everything below this
 * header is portable C + LVGL: it reaches hardware only through petal_hal.h.
 */
#ifndef LOTUS_APP_H
#define LOTUS_APP_H

#include <stdbool.h>

/**
 * Build the UI, wire input to it, and start the boot flow. Returns as soon as
 * that is done — from then on the app is driven entirely by LVGL timers and
 * input events. Must be called with the LVGL lock NOT held.
 */
void app_run(void);

/**
 * True when the touch release being handled right now was a TAP rather than a
 * swipe.
 *
 * Screens with a full-face tap target need this: a swipe both starts and ends on
 * that object, so LVGL reports a click for it too. Comparing against the press
 * origin is the only reliable way to tell them apart on the reference panel,
 * whose own gesture reporting cannot be trusted.
 */
bool app_touch_release_was_tap(void);

#endif /* LOTUS_APP_H */
