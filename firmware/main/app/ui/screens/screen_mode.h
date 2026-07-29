/*
 * screen_mode.h — the opening choice: track this table locally, or link several
 * dials together over ESP-NOW. Replaces the old "how many players?" prompt as
 * the first thing shown, since in Remote the pod size is discovered, not asked.
 */
#ifndef LOTUS_SCREEN_MODE_H
#define LOTUS_SCREEN_MODE_H

void screen_mode_init(void);
void screen_mode_open(void);

#endif /* LOTUS_SCREEN_MODE_H */
