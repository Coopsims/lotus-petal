/*
 * ui_common.h — the shared visual language, and the helpers every screen uses.
 *
 * Sizes come from the panel geometry the HAL reports rather than being written
 * in twice. The palette is deliberately small: a dark face, one accent, one
 * danger colour, one highlight. Anything that needs a fifth colour probably
 * needs rethinking instead.
 */
#ifndef LOTUS_UI_COMMON_H
#define LOTUS_UI_COMMON_H

#include "lvgl.h"
#include "petal_hal.h"

/* The face is square in memory and round in the glass; screens keep their
 * content inside the inscribed circle. */
#define UI_DIM PETAL_DISP_W

/* Product name, shown on the splash, the mode picker and About.
 *
 * The VERSION deliberately lives only in firmware/CMakeLists.txt (PROJECT_VER):
 * the About screen reads it back out of the built image through
 * petal_device_info(), so there is no second copy here to fall out of step with
 * what was actually built. */
#define UI_APP_NAME "Lotus Petal"

/* Palette (dark theme for the round display). */
#define UI_COL_BG        0x0A0A0F
#define UI_COL_TILE      0x16161F
#define UI_COL_TEXT      0xF5F5F7
#define UI_COL_MUTED     0x6B6B78
#define UI_COL_ACCENT    0x8B5CF6  /* violet — selection + click flash */
#define UI_COL_DANGER    0xEF4444  /* red — lethal thresholds */
#define UI_COL_GOLD      0xF4C430  /* monarch / day */

/**
 * ui_flash — fire the tactile "click" feedback on an overlay object.
 * Pulses the overlay's background opacity from a peak down to 0.
 */
void ui_flash(lv_obj_t *overlay);

/**
 * ui_make_flash_overlay — transparent, click-through overlay filling `parent`,
 * tinted `color`, ready to be pulsed by ui_flash().
 */
lv_obj_t *ui_make_flash_overlay(lv_obj_t *parent, uint32_t color);

/**
 * ui_make_round_screen — create a screen styled for the round display.
 * The whole framebuffer is painted with the dark face colour (the panel's
 * corners fall outside the round glass, so they are never seen); screens keep
 * their content inset within the inscribed circle. Returns the screen root
 * (non-scrollable, zero padding — set your own).
 */
lv_obj_t *ui_make_round_screen(void);

/**
 * ui_make_tap_zone — transparent, clickable overlay for touch input. Floats
 * above `parent`'s layout (size it with lv_obj_set_size + lv_obj_align); `cb`
 * runs on LV_EVENT_CLICKED with `user_data`. Swipes travel too far to register
 * as a click, so they bubble up to the screen root for navigation.
 */
lv_obj_t *ui_make_tap_zone(lv_obj_t *parent, lv_event_cb_t cb, void *user_data);

/**
 * ui_make_back_button — small round "<" that runs `cb`. Several screens are
 * entered from another screen and previously had no way out but a swipe, which
 * is not discoverable.
 */
lv_obj_t *ui_make_back_button(lv_obj_t *parent, lv_event_cb_t cb);

#endif /* LOTUS_UI_COMMON_H */
