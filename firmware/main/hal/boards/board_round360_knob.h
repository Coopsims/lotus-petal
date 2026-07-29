/*
 * board_round360_knob.h — reference board: a 360x360 round knob-and-touch panel
 * on an ESP32-S3.
 *
 * Silicon:  ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM.
 * Display:  ST77916 360x360 round IPS over QSPI.
 * Touch:    CST816 capacitive, I2C.
 * Dial:     detented rotary encoder on two GPIOs (see the decode note below).
 * Power:    single Li-ion cell sensed through a 10K/10K divider; no charge line.
 *
 * This is the JC3636W518-family knob module — sold under several names, and the
 * same panel Waveshare's ESP32-S3-Knob-Touch-LCD-1.8 uses. If your module has a
 * different pinout, copy this file rather than editing it.
 *
 * === The display recipe, which took real effort to find ===================
 * A clean image needs ALL FIVE of these. Miss any one and you get a black
 * screen, a backlight-only blank, or a striped/interlaced image:
 *
 *   1. The pins below (SCK 13, CS 14, D0-D3 = 15-18, RST 21, backlight 47).
 *   2. esp_lcd_st77916 v2, which is why the project needs ESP-IDF >= 5.4.
 *   3. SPI bus flags including SPICOMMON_BUSFLAG_GPIO_PINS, to force
 *      GPIO-matrix routing. The driver's own PANEL_BUS_QSPI_CONFIG macro sets
 *      no flags at all, and without this one the image stripes.
 *   4. The JC3636W518V2 init sequence in st77916_init.h — NOT the generic
 *      JC3636W518 sequence, which produces a two-on-two-off dual-gate interlace.
 *   5. RGB565 byte-swapped, invert_color(true), RGB element order.
 *
 * Sources that unlocked it: ESPHome's mipi_spi model JC3636W518V2,
 * KrX3D/WaveShare-Knob-Esp32S3 (working config + schematics), and the QSPI
 * thread on espressif/esp-bsp#764.
 */
#ifndef BOARD_ROUND360_KNOB_H
#define BOARD_ROUND360_KNOB_H

#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"

/* ---- panel ------------------------------------------------------------- */
#define BOARD_LCD_H_RES 360
#define BOARD_LCD_V_RES 360

/* ST77916 over QSPI: four data lines, no separate DC line. */
#define BOARD_LCD_SPI_HOST SPI2_HOST
#define BOARD_LCD_PIN_SCLK 13
#define BOARD_LCD_PIN_D0   15
#define BOARD_LCD_PIN_D1   16
#define BOARD_LCD_PIN_D2   17
#define BOARD_LCD_PIN_D3   18
#define BOARD_LCD_PIN_CS   14
#define BOARD_LCD_PIN_RST  21
#define BOARD_LCD_PIN_BL   47   /* backlight; PWM-capable, drive high = on */

/* Scanlines per transfer buffer. 80 rows of RGB565 is ~57 KB: fits DMA
 * comfortably and keeps tearing invisible at this refresh rate. */
#define BOARD_LCD_BUF_LINES 80

/* ---- touch ------------------------------------------------------------- */
/* CST816 on I2C. Two things to know about this part:
 *   - Its own gesture reporting is unreliable, so swipes are detected in the app
 *     from press->release displacement instead.
 *   - Raw coordinates drift towards the edges, differently on each unit, which
 *     is what the per-device calibration map exists for. */
#define BOARD_TOUCH_PIN_SDA 11
#define BOARD_TOUCH_PIN_SCL 12
#define BOARD_TOUCH_PIN_INT 9
#define BOARD_TOUCH_PIN_RST 10

/* Fallback touch map for an uncalibrated unit: display = scale * raw + offset.
 * Hand-measured on the reference hardware — accurate at the centre, drifting at
 * the edges, which is good enough to reach Settings > Calibrate Touch. */
#define BOARD_TOUCH_CAL_XSCALE 0.892f
#define BOARD_TOUCH_CAL_XOFF   30.0f
#define BOARD_TOUCH_CAL_YSCALE 1.085f
#define BOARD_TOUCH_CAL_YOFF   (-31.5f)

/* ---- dial -------------------------------------------------------------- */
/* A detented encoder that rests with BOTH lines high, and does NOT behave like
 * a textbook quadrature encoder — read the decode in hal_dial.c before changing
 * anything here. */
#define BOARD_ENC_PIN_A 8
#define BOARD_ENC_PIN_B 7

/* ---- battery ----------------------------------------------------------- */
/* Cell -> 10K/10K divider -> GPIO1 (ADC1_CH0), so the pin reads half the cell
 * voltage. There is no charge-status line to the SoC: while plugged in the pin
 * senses the held-high rail (~4.8 V) rather than the cell, so charging is
 * detected as "impossibly high for a cell" and the level is not readable. */
#define BOARD_BATT_ADC_UNIT    ADC_UNIT_1
#define BOARD_BATT_ADC_CHANNEL ADC_CHANNEL_0
#define BOARD_BATT_ADC_ATTEN   ADC_ATTEN_DB_12
#define BOARD_BATT_DIVIDER     2
#define BOARD_BATT_CHARGING_MV 4400

#endif /* BOARD_ROUND360_KNOB_H */
