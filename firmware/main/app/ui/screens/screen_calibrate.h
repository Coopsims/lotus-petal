/*
 * screen_calibrate.h — on-device touch calibration overlay.
 *
 * Shows a sequence of crosshair targets; the player taps each one, and a linear
 * (scale+offset per axis) least-squares fit of raw->display is computed and
 * saved to NVS via touch_calib. Reached from Settings > Calibrate Touch.
 */
#ifndef LOTUS_SCREEN_CALIBRATE_H
#define LOTUS_SCREEN_CALIBRATE_H

void screen_calibrate_init(void);
void screen_calibrate_open(void);

#endif /* LOTUS_SCREEN_CALIBRATE_H */
