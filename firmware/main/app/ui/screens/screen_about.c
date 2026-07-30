#include "screen_about.h"

#include "ui_common.h"
#include "nav.h"
#include "lotus.h"

#include "petal_hal.h"

#include <stdio.h>

#define UI_APP_AUTHOR "Coopsims"

static lv_obj_t *s_root;

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    nav_pop();
}

/* One "label / value" pair, stacked like a phone's about page. */
static void add_row(lv_obj_t *parent, const char *key, const char *value)
{
    lv_obj_t *k = lv_label_create(parent);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_font(k, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(k, lv_color_hex(UI_COL_MUTED), 0);

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_pad_bottom(v, 8, 0);
}

void screen_about_init(void)
{
    s_root = ui_make_round_screen();
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_ver(s_root, 52, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_root, 2, 0);

    lv_obj_t *mark = ui_lotus_create(s_root, 86);
    if (mark) lv_obj_set_style_pad_bottom(mark, 6, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, UI_APP_NAME);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_GOLD), 0);
    lv_obj_set_style_pad_bottom(title, 10, 0);

    /* Everything here comes from the platform rather than being compiled in, so the
     * page is honest about the image it is actually running. */
    petal_device_info_t dev;
    petal_device_info(&dev);

    char buf[40];
    if (dev.app_version[0]) {
        snprintf(buf, sizeof(buf), "v%s", dev.app_version);
        add_row(s_root, "Version", buf);
    }
    add_row(s_root, "Author", UI_APP_AUTHOR);

    /* Split across two lines: a full address does not fit the width of a round
     * face at a readable size. This is the same address the other dials identify
     * us by, which is what makes it worth showing at all. */
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X\n%02X:%02X:%02X",
             dev.mac[0], dev.mac[1], dev.mac[2], dev.mac[3], dev.mac[4], dev.mac[5]);
    add_row(s_root, "Address", buf);

    if (dev.chip[0])       add_row(s_root, "Hardware", dev.chip);
    if (dev.build_date[0]) add_row(s_root, "Built", dev.build_date);
    if (dev.sdk[0])        add_row(s_root, "SDK", dev.sdk);

    lv_obj_t *back = lv_button_create(s_root);
    lv_obj_set_size(back, 120, 44);
    lv_obj_set_style_radius(back, 22, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_pad_top(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_center(bl);
}

static void about_enc(int delta)
{
    lv_obj_scroll_by_bounded(s_root, 0, -delta * 40, LV_ANIM_ON);
}

void screen_about_open(void)
{
    lv_obj_scroll_to_y(s_root, 0, LV_ANIM_OFF);
    nav_push(s_root, about_enc);
}
