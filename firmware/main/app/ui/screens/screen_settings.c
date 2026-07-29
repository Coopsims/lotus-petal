#include "screen_settings.h"

#include "ui_common.h"
#include "nav.h"
#include "petal_hal.h"
#include "screen_calibrate.h"
#include "screen_update.h"
#include "screen_pair.h"
#include "screen_about.h"

#include "lvgl.h"

static lv_obj_t *s_root;

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    petal_backlight_set((int)lv_slider_get_value(slider));
}

/* Save once the finger lifts — persisting on every drag step would needlessly
 * churn the flash. */
static void brightness_released_cb(lv_event_t *e)
{
    (void)e;
    petal_backlight_save();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    nav_pop();
}

/* Push the calibration overlay on the next frame rather than nested inside this
 * button's touch dispatch — swapping the loaded screen mid-dispatch is the LVGL
 * re-entrancy that froze the device elsewhere. */
static void open_calibrate_async(void *p)
{
    (void)p;
    screen_calibrate_open();
}

static void calibrate_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_calibrate_async, NULL);
}

static void open_update_async(void *p)
{
    (void)p;
    screen_update_open();
}

static void update_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_update_async, NULL);
}

static void open_link_async(void *p)
{
    (void)p;
    screen_pair_open_link();
}

/* Pushing firmware needs the dials linked, but not a game — so linking is
 * reachable here without setting a table up first. */
static void link_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_link_async, NULL);
}

static void open_about_async(void *p)
{
    (void)p;
    screen_about_open();
}

static void about_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_about_async, NULL);
}

static void settings_enc(int delta)
{
    /* Bounded scroll: clamps to the content so the dial can't spin the page off
     * past the top or bottom (plain lv_obj_scroll_by is unbounded). */
    lv_obj_scroll_by_bounded(s_root, 0, -delta * 40, LV_ANIM_ON);
}

void screen_settings_init(void)
{
    s_root = ui_make_round_screen();
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_ver(s_root, 44, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_root, 12, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_pad_bottom(title, 4, 0);

    /* --- Brightness --- */
    lv_obj_t *bl_cap = lv_label_create(s_root);
    lv_label_set_text(bl_cap, LV_SYMBOL_EYE_OPEN "  Brightness");
    lv_obj_set_style_text_font(bl_cap, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bl_cap, lv_color_hex(UI_COL_MUTED), 0);

    lv_obj_t *slider = lv_slider_create(s_root);
    lv_obj_set_size(slider, 210, 14);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, petal_backlight_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COL_TILE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COL_GOLD), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COL_GOLD), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, brightness_released_cb, LV_EVENT_RELEASED, NULL);

    /* --- Calibrate Touch --- */
    lv_obj_t *cal = lv_button_create(s_root);
    lv_obj_set_size(cal, 210, 44);
    lv_obj_set_style_radius(cal, 22, 0);
    lv_obj_set_style_bg_color(cal, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(cal, 0, 0);
    lv_obj_add_event_cb(cal, calibrate_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cal_lbl = lv_label_create(cal);
    lv_label_set_text(cal_lbl, LV_SYMBOL_GPS "  Calibrate Touch");
    lv_obj_set_style_text_font(cal_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cal_lbl, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(cal_lbl);

    /* --- Link petals --- */
    lv_obj_t *lnk = lv_button_create(s_root);
    lv_obj_set_size(lnk, 210, 44);
    lv_obj_set_style_radius(lnk, 22, 0);
    lv_obj_set_style_bg_color(lnk, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(lnk, 0, 0);
    lv_obj_add_event_cb(lnk, link_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lnk_lbl = lv_label_create(lnk);
    lv_label_set_text(lnk_lbl, LV_SYMBOL_WIFI "  Link Petals");
    lv_obj_set_style_text_font(lnk_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lnk_lbl, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(lnk_lbl);

    /* --- Firmware (send to / receive from a linked petal) --- */
    lv_obj_t *upd = lv_button_create(s_root);
    lv_obj_set_size(upd, 210, 44);
    lv_obj_set_style_radius(upd, 22, 0);
    lv_obj_set_style_bg_color(upd, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(upd, 0, 0);
    lv_obj_add_event_cb(upd, update_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *upd_lbl = lv_label_create(upd);
    lv_label_set_text(upd_lbl, LV_SYMBOL_DOWNLOAD "  Firmware");
    lv_obj_set_style_text_font(upd_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(upd_lbl, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(upd_lbl);

    /* --- About --- */
    lv_obj_t *about = lv_button_create(s_root);
    lv_obj_set_size(about, 210, 44);
    lv_obj_set_style_radius(about, 22, 0);
    lv_obj_set_style_bg_color(about, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(about, 0, 0);
    lv_obj_add_event_cb(about, about_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *about_lbl = lv_label_create(about);
    lv_label_set_text(about_lbl, LV_SYMBOL_LIST "  About");
    lv_obj_set_style_text_font(about_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(about_lbl, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(about_lbl);

    /* --- Back --- */
    lv_obj_t *back = lv_button_create(s_root);
    lv_obj_set_size(back, 120, 44);
    lv_obj_set_style_radius(back, 22, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_18, 0);
    lv_obj_center(bl);
}

void screen_settings_open(void)
{
    lv_obj_scroll_to_y(s_root, 0, LV_ANIM_OFF);
    nav_push(s_root, settings_enc);
}
