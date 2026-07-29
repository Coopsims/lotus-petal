/*
 * screen_splash.h — boot splash: a white line-art lotus on the dark face, with
 * the product name beneath it.
 *
 * The lotus is drawn from polylines generated at runtime rather than shipped as
 * a bitmap, so it costs a few hundred bytes instead of a decoded image and stays
 * crisp at any size.
 */
#ifndef LOTUS_SCREEN_SPLASH_H
#define LOTUS_SCREEN_SPLASH_H

typedef void (*splash_done_fn)(void);

void screen_splash_init(void);

/** Show the splash, then call `done` once it has been on screen long enough.
 *  `done` runs outside the LVGL event dispatch, so it is safe to load screens. */
void screen_splash_open(splash_done_fn done);

#endif /* LOTUS_SCREEN_SPLASH_H */
