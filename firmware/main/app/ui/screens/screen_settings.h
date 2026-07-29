/*
 * screen_settings.h — the Settings overlay reached from the Tools pie.
 *
 * A scrollable page holding the device settings: brightness and an About block
 * with the product name + firmware version.
 * Built once at startup; opened as an overlay on top of the pie.
 */
#ifndef LOTUS_SCREEN_SETTINGS_H
#define LOTUS_SCREEN_SETTINGS_H

/** Build the Settings overlay once (call at startup). */
void screen_settings_init(void);

/** Push the Settings overlay. */
void screen_settings_open(void);

#endif /* LOTUS_SCREEN_SETTINGS_H */
