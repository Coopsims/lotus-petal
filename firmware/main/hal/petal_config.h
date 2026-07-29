/*
 * petal_config.h — the handful of hardware facts the application is allowed to
 * know, and the switches that turn optional hardware off.
 *
 * This is the ONLY hardware-shaped header the app layer includes (via
 * petal_hal.h). It deliberately contains no pins, no register values and no
 * vendor SDK types — just geometry and capability flags, so a port can change
 * the hardware without the UI needing to know how.
 *
 * When porting: set the geometry to match your panel and clear the capability
 * flags for anything your board does not have. Keep it in step with your board
 * header (hal/boards/board.h) — hal_display.c static-asserts that the two agree,
 * so a mismatch is a build error rather than a blank screen.
 */
#ifndef PETAL_CONFIG_H
#define PETAL_CONFIG_H

/* ---- display geometry ----------------------------------------------------
 * The UI is laid out in real pixels for a round face this size. A different
 * size builds and runs, but the layout constants in app/ui are tuned for
 * 360x360 and want revisiting — see docs/PORTING.md. */
#define PETAL_DISP_W 360
#define PETAL_DISP_H 360

/* 1 = the glass is a circle inscribed in the framebuffer, so the corners are
 * never seen and screens keep their content inside the inscribed circle. */
#define PETAL_DISP_ROUND 1

/* ---- optional hardware --------------------------------------------------
 * Clear any of these and the app hides the corresponding UI rather than showing
 * a dead control. The three the app cannot do without are the dial, the touch
 * panel and the radio. */
#define PETAL_HAS_BACKLIGHT 1  /* dimmable backlight -> Settings brightness slider */
#define PETAL_HAS_BATTERY   1  /* battery sense      -> gauge on the Life screen   */

/* ---- input feel --------------------------------------------------------- */

/* Hold the middle of the face this long to open the Tools pie (ms). */
#define PETAL_LONG_PRESS_MS 700

/* A press->release that moves no further than this counts as a tap rather than
 * a swipe (pixels). Raise it on a panel with noisier coordinates. */
#define PETAL_TAP_SLOP_PX 22

/* Horizontal travel needed to read a drag as a screen-changing swipe (pixels). */
#define PETAL_SWIPE_MIN_PX 55

#endif /* PETAL_CONFIG_H */
