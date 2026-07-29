#include "screen_setup.h"

#include "ui_common.h"
#include "nav.h"
#include "game.h"
#include "screen_mode.h"

#include "lvgl.h"
#include <stdint.h>

/* Player counts offered, in order. Index 0..6 maps to 2..8 players. */
#define OPT_MIN     2
#define OPT_MAX     8
#define OPT_COUNT   (OPT_MAX - OPT_MIN + 1)

static lv_obj_t *s_root;
static lv_obj_t *s_btn[OPT_COUNT];
static int       s_sel;

static void update_highlight(void)
{
    for (int i = 0; i < OPT_COUNT; i++) {
        bool sel = (i == s_sel);
        lv_obj_set_style_bg_color(s_btn[i],
                                  lv_color_hex(sel ? UI_COL_ACCENT : UI_COL_TILE), 0);
        lv_obj_set_style_border_width(s_btn[i], sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(s_btn[i],
                                      lv_color_hex(sel ? UI_COL_TEXT : UI_COL_MUTED), 0);
    }
}

/* Deferred tail of choose(): rebuild the commander grid for the new pod size and
 * pop back to the game. Run via lv_async_call so the heavy rebuild + screen swap
 * happen at the top of the next frame rather than nested inside the button's
 * touch-event dispatch — swapping the active screen mid-dispatch (while the tap
 * that triggered it is still being processed on the screen being torn down) is
 * the LVGL re-entrancy that froze the device on "new game". */
static void commit_choice(void *p)
{
    int idx = (int)(intptr_t)p;
    game_set_player_count(OPT_MIN + idx);
    nav_pop();
}

/* Choose a player count, configure the game, and return to the Life screen. */
static void choose(int idx)
{
    s_sel = idx;
    update_highlight();
    lv_async_call(commit_choice, (void *)(intptr_t)idx);
}

static void back_async(void *p)
{
    (void)p;
    nav_pop();
    screen_mode_open();   /* back to Local / Remote */
}
static void back_cb(lv_event_t *e) { (void)e; lv_async_call(back_async, NULL); }

static void btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    choose(idx);
}

/* Dial just moves the highlight for feedback; the choice is committed by a tap
 * (this unit has no encoder button). */
static void setup_enc(int delta)
{
    s_sel = ((s_sel + delta) % OPT_COUNT + OPT_COUNT) % OPT_COUNT;
    update_highlight();
}

void screen_setup_init(void)
{
    s_root = ui_make_round_screen();
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_root, 6, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "PLAYERS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_TEXT), 0);

    lv_obj_t *sub = lv_label_create(s_root);
    lv_label_set_text(sub, "How many at the table?");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_set_style_pad_bottom(sub, 8, 0);

    /* Number buttons in a centred flex-wrap box; capping the width to three
     * columns lays 2..6 out as 2 3 4 / 5 6. */
    lv_obj_t *grid = lv_obj_create(s_root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 3 * 66 + 2 * 12, LV_SIZE_CONTENT);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(grid, 12, 0);

    lv_obj_t *back = ui_make_back_button(s_root, back_cb);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -12);

    for (int i = 0; i < OPT_COUNT; i++) {
        lv_obj_t *btn = lv_button_create(grid);
        lv_obj_set_size(btn, 66, 66);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_btn[i] = btn;

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%d", OPT_MIN + i);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_TEXT), 0);
        lv_obj_center(lbl);
    }
}

void screen_setup_open(void)
{
    /* Default the highlight to the current pod size. */
    s_sel = game_player_count() - OPT_MIN;
    if (s_sel < 0) s_sel = 0;
    if (s_sel >= OPT_COUNT) s_sel = OPT_COUNT - 1;
    update_highlight();
    nav_push(s_root, setup_enc);
}
