#include "screen_mode.h"

#include "ui_common.h"
#include "nav.h"
#include "game.h"
#include "screen_setup.h"
#include "screen_pair.h"
#include "screen_settings.h"

#include <stdint.h>

#define OPT_COUNT 2

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

/* Deferred so the screen swap happens at the top of the next frame instead of
 * nested inside the button's touch dispatch (see screen_setup.c). */
static void commit(void *p)
{
    int idx = (int)(intptr_t)p;
    if (idx == 0) {
        game_set_mode(GAME_MODE_LOCAL);
        nav_pop();          /* back to the game, then ask for the pod size */
        screen_setup_open();
    } else {
        game_set_mode(GAME_MODE_REMOTE);
        nav_pop();
        screen_pair_open();
    }
}

static void choose(int idx)
{
    s_sel = idx;
    update_highlight();
    lv_async_call(commit, (void *)(intptr_t)idx);
}

static void btn_cb(lv_event_t *e)
{
    choose((int)(intptr_t)lv_event_get_user_data(e));
}

static void open_settings_async(void *p)
{
    (void)p;
    screen_settings_open();
}

/* Settings used to be reachable only by holding the face mid-game; from the
 * opening menu it is available before a game starts too. */
static void settings_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(open_settings_async, NULL);
}

static void mode_enc(int delta)
{
    s_sel = ((s_sel + delta) % OPT_COUNT + OPT_COUNT) % OPT_COUNT;
    update_highlight();
}

static lv_obj_t *make_option(lv_obj_t *parent, int idx, const char *icon,
                             const char *title, const char *sub)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 250, 82);
    lv_obj_set_style_radius(btn, 20, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *head = lv_label_create(btn);
    lv_label_set_text_fmt(head, "%s  %s", icon, title);
    lv_obj_set_style_text_font(head, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(head, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_align(head, LV_ALIGN_CENTER, 0, -14);

    lv_obj_t *hint = lv_label_create(btn);
    lv_label_set_text(hint, sub);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 16);

    s_btn[idx] = btn;
    return btn;
}

void screen_mode_init(void)
{
    s_root = ui_make_round_screen();
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_root, 14, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, UI_APP_NAME);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_GOLD), 0);
    lv_obj_set_style_pad_bottom(title, 2, 0);

    make_option(s_root, 0, LV_SYMBOL_HOME, "Local",  "this dial only");
    make_option(s_root, 1, LV_SYMBOL_WIFI, "Remote", "link other dials");

    /* Compact gear below the two choices — floats clear of the flex column so it
     * hugs the bottom of the round face. */
    lv_obj_t *cog = lv_button_create(s_root);
    lv_obj_add_flag(cog, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(cog, 46, 46);
    lv_obj_set_style_radius(cog, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cog, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(cog, 0, 0);
    /* No back button here: this is where the flow starts, so there is nowhere
     * behind it to go. The screens it leads to come back to it instead. */
    lv_obj_align(cog, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_event_cb(cog, settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cog);
    lv_label_set_text(cl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(cl, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_center(cl);

    s_sel = 0;
    update_highlight();
}

void screen_mode_open(void)
{
    s_sel = (game_mode() == GAME_MODE_REMOTE) ? 1 : 0;
    update_highlight();
    nav_push(s_root, mode_enc);
}
