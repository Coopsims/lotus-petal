/*
 * hal_internal.h — bring-up entry points shared between the files of this port.
 *
 * Not part of the contract in petal_hal.h and not visible to app/: it only
 * exists so platform_main.c can bring the devices up in the right order.
 */
#ifndef HAL_INTERNAL_H
#define HAL_INTERNAL_H

#include <stdbool.h>

#include "lvgl.h"

/** Panel + LVGL display + backlight. False if there is no usable screen. */
bool hal_display_init(void);

/** Touch panel on top of `disp`. False if the controller did not answer — the
 *  app then runs dial-only rather than not at all. */
bool hal_touch_init(lv_display_t *disp);

/** Encoder GPIOs and their interrupt. */
void hal_dial_init(void);

/** Battery ADC and its calibration scheme. Harmless on a board without one. */
void hal_battery_init(void);

/** Key/value storage. Must run before anything reads a setting. */
bool hal_storage_init(void);

/** Optional discharge logger for fitting a battery curve; compiled out unless
 *  the build defines LOTUS_BATTERY_CALIB. */
void hal_battery_log_init(void);

#endif /* HAL_INTERNAL_H */
