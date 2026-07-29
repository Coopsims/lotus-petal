/*
 * hal_display.c — ST77916 QSPI panel + LVGL display, and the PWM backlight.
 *
 * The five-part display recipe this depends on is documented in
 * boards/board_round360_knob.h; the code below is the other half of it. If you
 * change any of it and the image stripes or interlaces, that comment is why.
 */
#include "petal_hal.h"
#include "boards/board.h"
#include "boards/st77916_init.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

/* The app lays out in PETAL_DISP_* and the driver in BOARD_LCD_*; a silent
 * disagreement between them shows up as a display that is subtly wrong rather
 * than obviously broken, so make it a build error instead. */
_Static_assert(PETAL_DISP_W == BOARD_LCD_H_RES, "petal_config.h disagrees with the board width");
_Static_assert(PETAL_DISP_H == BOARD_LCD_V_RES, "petal_config.h disagrees with the board height");

static const char *TAG = "hal.disp";

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;
static lv_display_t             *s_disp;

/* ---------- backlight ----------------------------------------------------
 * LEDC PWM on the backlight pin. The panel backlight is by far the largest
 * current draw on a battery-powered dial, so this is the one knob that
 * meaningfully changes runtime. */

#define BL_MODE  LEDC_LOW_SPEED_MODE
#define BL_TIMER LEDC_TIMER_0
#define BL_CH    LEDC_CHANNEL_0
#define BL_RES   LEDC_TIMER_8_BIT
#define BL_MAX   255

#define BL_NS  "settings"
#define BL_KEY "brightness"

static int s_bl_pct = 100;

#if PETAL_HAS_BACKLIGHT
static int backlight_load_saved(void)
{
    uint8_t v = 100;
    if (!petal_kv_get(BL_NS, BL_KEY, &v, sizeof(v))) return 100;
    if (v < 5)   v = 5;
    if (v > 100) v = 100;
    return v;
}
#endif

void petal_backlight_set(int pct)
{
#if PETAL_HAS_BACKLIGHT
    if (pct < 5)   pct = 5;     /* never fully dark — that reads as a dead device */
    if (pct > 100) pct = 100;
    s_bl_pct = pct;
    ledc_set_duty(BL_MODE, BL_CH, (uint32_t)pct * BL_MAX / 100);
    ledc_update_duty(BL_MODE, BL_CH);
#else
    (void)pct;
#endif
}

int petal_backlight_get(void)
{
    return s_bl_pct;
}

void petal_backlight_save(void)
{
#if PETAL_HAS_BACKLIGHT
    uint8_t v = (uint8_t)s_bl_pct;
    petal_kv_set(BL_NS, BL_KEY, &v, sizeof(v));
#endif
}

static void backlight_init(void)
{
#if PETAL_HAS_BACKLIGHT
    const ledc_timer_config_t tcfg = {
        .speed_mode      = BL_MODE,
        .duty_resolution = BL_RES,
        .timer_num       = BL_TIMER,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);

    const ledc_channel_config_t ccfg = {
        .gpio_num   = BOARD_LCD_PIN_BL,
        .speed_mode = BL_MODE,
        .channel    = BL_CH,
        .timer_sel  = BL_TIMER,
        .duty       = BL_MAX,
        .hpoint     = 0,
    };
    ledc_channel_config(&ccfg);

    petal_backlight_set(backlight_load_saved());
#endif
}

/* ---------- panel -------------------------------------------------------- */

bool hal_display_init(void)
{
    backlight_init();   /* before the panel, so bring-up is not shown in the dark */

    const spi_bus_config_t buscfg = {
        .sclk_io_num  = BOARD_LCD_PIN_SCLK,
        .data0_io_num = BOARD_LCD_PIN_D0,
        .data1_io_num = BOARD_LCD_PIN_D1,
        .data2_io_num = BOARD_LCD_PIN_D2,
        .data3_io_num = BOARD_LCD_PIN_D3,
        .data4_io_num = -1, .data5_io_num = -1,
        .data6_io_num = -1, .data7_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_BUF_LINES * (int)sizeof(uint16_t),
        /* GPIO_PINS is the non-obvious one: it forces GPIO-matrix routing for
         * pins that are not on the IO_MUX. Without it the image stripes, and the
         * driver's convenience macro does not set it. */
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD |
                 SPICOMMON_BUSFLAG_GPIO_PINS,
    };
    if (spi_bus_initialize(BOARD_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "spi bus init failed");
        return false;
    }

    const esp_lcd_panel_io_spi_config_t io_config =
        ST77916_PANEL_IO_QSPI_CONFIG(BOARD_LCD_PIN_CS, NULL, NULL);
    if (esp_lcd_new_panel_io_spi(BOARD_LCD_SPI_HOST, &io_config, &s_io) != ESP_OK) {
        ESP_LOGE(TAG, "panel io failed");
        return false;
    }

    st77916_vendor_config_t vendor = {
        .init_cmds      = board_lcd_init_cmds,
        .init_cmds_size = sizeof(board_lcd_init_cmds) / sizeof(board_lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor,
    };
    if (esp_lcd_new_panel_st77916(s_io, &panel_cfg, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed");
        return false;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    /* esp_lvgl_port owns the LVGL task and tick, which is what petal_lvgl_lock()
     * guards. One buffer is enough: the UI redraws only dirty regions. */
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&port_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl port init failed");
        return false;
    }
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = s_io,
        .panel_handle = s_panel,
        .buffer_size  = BOARD_LCD_H_RES * BOARD_LCD_BUF_LINES,
        .double_buffer = false,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        /* swap_bytes: this panel wants RGB565 byte-swapped. Letting the port do
         * it beats swapping pixels by hand. */
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    return s_disp != NULL;
}

lv_display_t *petal_display(void)
{
    return s_disp;
}

esp_lcd_panel_io_handle_t hal_display_panel_io(void)
{
    return s_io;
}

void petal_lvgl_lock(void)
{
    lvgl_port_lock(0);
}

void petal_lvgl_unlock(void)
{
    lvgl_port_unlock();
}
