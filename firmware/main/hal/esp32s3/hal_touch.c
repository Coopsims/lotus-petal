/*
 * hal_touch.c — CST816 capacitive touch, plus the per-device calibration map.
 *
 * Raw panel coordinates do not land on display pixels, and the error differs
 * from unit to unit, so every sample goes through a per-axis linear map
 * (display = scale * raw + offset) on its way to LVGL. The coefficients live in
 * storage; Settings > Calibrate Touch fits new ones. The board's hand-measured
 * defaults are only good enough to get you to that screen.
 */
#include "petal_hal.h"
#include "boards/board.h"

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include <string.h>

static const char *TAG = "hal.touch";

#define CALIB_NS    "touchcal"
#define CALIB_KEY   "coef"
#define CALIB_MAGIC 0x54434131u  /* 'TCA1' — rejects a blob from another layout */

static esp_lcd_touch_handle_t s_tp;
static lv_indev_t            *s_indev;

static float s_coef[PETAL_TOUCH_CALIB_N];

/* Written from the touch driver's callback, read by the calibration screen on
 * the LVGL thread. A torn read would cost one stale crosshair sample, which the
 * screen would simply re-take, so this needs no heavier synchronisation. */
static volatile uint16_t s_last_raw_x, s_last_raw_y;

typedef struct {
    uint32_t magic;
    float    coef[PETAL_TOUCH_CALIB_N];
} calib_blob_t;

void petal_touch_calib_defaults(float coef[PETAL_TOUCH_CALIB_N])
{
    coef[0] = BOARD_TOUCH_CAL_XSCALE;
    coef[1] = BOARD_TOUCH_CAL_XOFF;
    coef[2] = BOARD_TOUCH_CAL_YSCALE;
    coef[3] = BOARD_TOUCH_CAL_YOFF;
}

void petal_touch_calib_get(float coef[PETAL_TOUCH_CALIB_N])
{
    memcpy(coef, s_coef, sizeof(s_coef));
}

bool petal_touch_calib_set(const float coef[PETAL_TOUCH_CALIB_N])
{
    calib_blob_t blob = { .magic = CALIB_MAGIC };
    memcpy(blob.coef, coef, sizeof(blob.coef));
    if (!petal_kv_set(CALIB_NS, CALIB_KEY, &blob, sizeof(blob))) return false;
    memcpy(s_coef, coef, sizeof(s_coef));
    return true;
}

void petal_touch_last_raw(uint16_t *raw_x, uint16_t *raw_y)
{
    if (raw_x) *raw_x = s_last_raw_x;
    if (raw_y) *raw_y = s_last_raw_y;
}

/* Load the saved map, or fall back to the board defaults. Must run before the
 * panel starts producing samples. */
static void calib_load(void)
{
    petal_touch_calib_defaults(s_coef);

    calib_blob_t blob;
    if (petal_kv_get(CALIB_NS, CALIB_KEY, &blob, sizeof(blob)) &&
        blob.magic == CALIB_MAGIC) {
        memcpy(s_coef, blob.coef, sizeof(s_coef));
    }
}

/* Runs inside the touch driver for every sample: remember the raw point for the
 * calibration screen, then map to display pixels and clamp to the panel. */
static void process_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                                uint16_t *strength, uint8_t *point_num,
                                uint8_t max_point_num)
{
    (void)tp;
    (void)strength;

    uint8_t n = point_num ? *point_num : 0;
    if (n > 0 && x && y) {
        s_last_raw_x = x[0];
        s_last_raw_y = y[0];
    }

    for (uint8_t i = 0; i < n && i < max_point_num; i++) {
        float cx = s_coef[0] * (float)x[i] + s_coef[1];
        float cy = s_coef[2] * (float)y[i] + s_coef[3];
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx > PETAL_DISP_W - 1) cx = PETAL_DISP_W - 1;
        if (cy > PETAL_DISP_H - 1) cy = PETAL_DISP_H - 1;
        x[i] = (uint16_t)(cx + 0.5f);
        y[i] = (uint16_t)(cy + 0.5f);
    }
}

bool hal_touch_init(lv_display_t *disp)
{
    calib_load();

    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = BOARD_TOUCH_PIN_SDA,
        .scl_io_num = BOARD_TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "i2c bus failed");
        return false;
    }

    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    if (esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io) != ESP_OK) {
        ESP_LOGW(TAG, "touch io failed");
        return false;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = PETAL_DISP_W,
        .y_max = PETAL_DISP_H,
        .rst_gpio_num = BOARD_TOUCH_PIN_RST,
        .int_gpio_num = BOARD_TOUCH_PIN_INT,
        .process_coordinates = process_coordinates,
    };
    if (esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_tp) != ESP_OK) {
        ESP_LOGW(TAG, "cst816 not found");
        return false;
    }

    const lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = s_tp };
    s_indev = lvgl_port_add_touch(&touch_cfg);
    ESP_LOGI(TAG, "touch ready");
    return s_indev != NULL;
}

lv_indev_t *petal_touch_indev(void)
{
    return s_indev;
}
