/*
 * screen_update.h — firmware transfer overlay: offer an image to a linked dial,
 * or accept one being offered to us, with progress for either direction.
 * Reached from Settings, and opened by itself when an offer arrives.
 */
#ifndef LOTUS_SCREEN_UPDATE_H
#define LOTUS_SCREEN_UPDATE_H

#include <stdbool.h>

void screen_update_init(void);
void screen_update_open(void);

/** True while this screen is the one on display. */
bool screen_update_is_open(void);

#endif /* LOTUS_SCREEN_UPDATE_H */
