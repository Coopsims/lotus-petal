#include "screen_result.h"

#include "ui_common.h"
#include "nav.h"
#include "game.h"

static lv_obj_t *s_root;
static lv_obj_t *s_title;
static lv_obj_t *s_sub;

static void again_async(void *p)
{
    LV_UNUSED(p);
    /* Keeps the dials linked — only the game is cleared. */
    game_begin_new_round();
    nav_pop_all();
}

static void again_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_async_call(again_async, NULL);
}

void screen_result_init(void)
{
    s_root = ui_make_round_screen();
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_root, 12, 0);

    s_title = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_36, 0);

    s_sub = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_sub, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_set_style_pad_bottom(s_sub, 8, 0);

    lv_obj_t *again = lv_button_create(s_root);
    lv_obj_set_size(again, 200, 52);
    lv_obj_set_style_radius(again, 26, 0);
    lv_obj_set_style_bg_color(again, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_shadow_width(again, 0, 0);
    lv_obj_add_event_cb(again, again_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *al = lv_label_create(again);
    lv_label_set_text(al, LV_SYMBOL_REFRESH "  New Round");
    lv_obj_set_style_text_font(al, &lv_font_montserrat_18, 0);
    lv_obj_center(al);
}

void screen_result_open(void)
{
    bool won = (game_result() == GAME_RESULT_WIN);
    lv_label_set_text(s_title, won ? "VICTORY" : "DEFEATED");
    lv_obj_set_style_text_color(s_title,
        lv_color_hex(won ? UI_COL_GOLD : UI_COL_DANGER), 0);
    lv_label_set_text(s_sub, won ? "last petal standing" : "better luck next game");
    nav_push(s_root, NULL);
}
