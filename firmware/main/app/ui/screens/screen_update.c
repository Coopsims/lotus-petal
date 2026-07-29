#include "screen_update.h"

#include "ui_common.h"
#include "nav.h"
#include "fw_push.h"

static lv_obj_t   *s_root;
static lv_obj_t   *s_title;
static lv_obj_t   *s_detail;
static lv_obj_t   *s_bar;
static lv_obj_t   *s_pct;
static lv_obj_t   *s_go;
static lv_obj_t   *s_go_lbl;
static lv_obj_t   *s_push;
static lv_obj_t   *s_push_lbl;
static lv_timer_t *s_poll;

static void refresh(void)
{
    /* A petal-to-petal transfer takes over the screen while it runs — it is the
     * more interesting thing to watch, and the two never overlap. */
    fw_state_t fw = fw_push_state();

    if (fw == FW_OFFERED) {
        /* Someone we are linked with wants to install firmware — that is not
         * something to do silently, so it needs a yes. */
        lv_label_set_text(s_title, "ACCEPT UPDATE?");
        lv_label_set_text(s_detail, fw_push_detail());
        lv_obj_set_style_text_color(s_title, lv_color_hex(UI_COL_GOLD), 0);
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        lv_obj_add_flag(s_pct, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_go, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_go_lbl, LV_SYMBOL_OK "  Accept");
        lv_obj_remove_flag(s_push, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_push_lbl, LV_SYMBOL_CLOSE "  Decline");
        return;
    }

    if (fw != FW_IDLE) {
        const char *head = fw == FW_SENDING   ? "SENDING"
                         : fw == FW_RECEIVING ? "RECEIVING"
                         : fw == FW_APPLYING  ? "INSTALLING"
                         : fw == FW_OK        ? LV_SYMBOL_OK "  DONE"
                                              : LV_SYMBOL_WARNING "  FAILED";
        lv_label_set_text(s_title, head);
        lv_label_set_text(s_detail, fw_push_detail());
        lv_obj_set_style_text_color(s_title,
            lv_color_hex(fw == FW_ERROR ? UI_COL_DANGER
                       : fw == FW_OK    ? UI_COL_GOLD : UI_COL_TEXT), 0);
        lv_bar_set_value(s_bar, fw_push_percent(), LV_ANIM_OFF);
        lv_label_set_text_fmt(s_pct, "%d%%", fw_push_percent());
        lv_obj_remove_flag(s_pct, LV_OBJ_FLAG_HIDDEN);

        bool busy = (fw == FW_SENDING || fw == FW_RECEIVING || fw == FW_APPLYING);
        if (busy) { lv_obj_add_flag(s_go, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(s_push, LV_OBJ_FLAG_HIDDEN); }
        else      { lv_obj_remove_flag(s_go, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(s_push, LV_OBJ_FLAG_HIDDEN); }
        return;
    }

    /* Nothing in flight. */
    lv_label_set_text(s_title, "FIRMWARE");
    lv_label_set_text(s_detail, fw_push_available() ? "linked petal ready"
                                                    : "link a petal first");
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_COL_TEXT), 0);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_pct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_go, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_push, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_push_lbl, "Send to petal");
}

static void poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    refresh();
}

static void go_async(void *p)
{
    LV_UNUSED(p);
    if (fw_push_state() == FW_OFFERED) fw_push_accept();
    refresh();
}

static void go_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_async_call(go_async, NULL);
}

static void push_async(void *p)
{
    LV_UNUSED(p);
    if (fw_push_state() == FW_OFFERED) fw_push_decline();
    else                               fw_push_start();
    refresh();
}

static void push_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_async_call(push_async, NULL);
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    nav_pop();
}

void screen_update_init(void)
{
    s_root = ui_make_round_screen();
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_root, 10, 0);

    s_title = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_COL_TEXT), 0);

    s_detail = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_detail, lv_color_hex(UI_COL_MUTED), 0);

    s_bar = lv_bar_create(s_root);
    lv_obj_set_size(s_bar, 210, 14);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_COL_TILE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_COL_GOLD), LV_PART_INDICATOR);

    s_pct = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_pct, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_pct, lv_color_hex(UI_COL_TEXT), 0);

    s_go = lv_button_create(s_root);
    lv_obj_set_size(s_go, 170, 46);
    lv_obj_set_style_radius(s_go, 23, 0);
    lv_obj_set_style_bg_color(s_go, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_shadow_width(s_go, 0, 0);
    lv_obj_add_event_cb(s_go, go_cb, LV_EVENT_CLICKED, NULL);
    s_go_lbl = lv_label_create(s_go);
    lv_label_set_text(s_go_lbl, "Update");
    lv_obj_set_style_text_font(s_go_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(s_go_lbl);

    s_push = lv_button_create(s_root);
    lv_obj_set_size(s_push, 200, 44);
    lv_obj_set_style_radius(s_push, 22, 0);
    lv_obj_set_style_bg_color(s_push, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(s_push, 0, 0);
    lv_obj_add_event_cb(s_push, push_cb, LV_EVENT_CLICKED, NULL);
    s_push_lbl = lv_label_create(s_push);
    lv_label_set_text(s_push_lbl, "Send to petal");
    lv_obj_set_style_text_font(s_push_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_push_lbl, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(s_push_lbl);

    lv_obj_t *back = lv_button_create(s_root);
    lv_obj_set_size(back, 120, 42);
    lv_obj_set_style_radius(back, 21, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_center(bl);
}

bool screen_update_is_open(void)
{
    return s_root && lv_screen_active() == s_root;
}

void screen_update_open(void)
{
    refresh();
    if (!s_poll) s_poll = lv_timer_create(poll_cb, 300, NULL);
    lv_timer_resume(s_poll);
    nav_push(s_root, NULL);
}
