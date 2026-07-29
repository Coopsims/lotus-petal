#include "screen_order.h"

#include "ui_common.h"
#include "nav.h"
#include "game.h"

#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Positions laid out on a ring, matching the round face. First place is offered
 * whenever it is still free: with a player at the table who has no dial, the d20
 * roll-off among the dials cannot know they rolled higher, so somebody has to be
 * able to leave first place empty for them. */
#define MAX_POS   8
#define RING_R    116
#define SLOT_D    62

static lv_obj_t *s_root;
static lv_obj_t *s_slot[MAX_POS + 1];   /* indexed by position, 1..MAX_POS used */
static lv_obj_t *s_slot_lbl[MAX_POS + 1];
static lv_obj_t *s_title;

static void claim_async(void *p)
{
    game_claim_seat((int)(intptr_t)p);
    nav_pop();
}

static void back_async(void *p) { (void)p; nav_pop(); }
static void back_cb(lv_event_t *e) { (void)e; lv_async_call(back_async, NULL); }

static void slot_cb(lv_event_t *e)
{
    int pos = (int)(intptr_t)lv_event_get_user_data(e);
    lv_async_call(claim_async, (void *)(intptr_t)pos);
}

void screen_order_init(void)
{
    s_root = ui_make_round_screen();

    s_title = lv_label_create(s_root);
    lv_label_set_text(s_title, "PICK YOUR\nSEAT");
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(s_title);

    lv_obj_t *back = ui_make_back_button(s_root, back_cb);
    lv_obj_align(back, LV_ALIGN_CENTER, 0, 54);

    for (int pos = 1; pos <= MAX_POS; pos++) {
        lv_obj_t *b = lv_button_create(s_root);
        lv_obj_set_size(b, SLOT_D, SLOT_D);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_add_event_cb(b, slot_cb, LV_EVENT_CLICKED, (void *)(intptr_t)pos);
        s_slot[pos] = b;

        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
        lv_obj_center(l);
        s_slot_lbl[pos] = l;
    }
}

void screen_order_open(void)
{
    unsigned char taken[MAX_POS + 2];
    int players = game_seat_map(taken, MAX_POS + 2);
    if (players > MAX_POS) players = MAX_POS;

    /* Only the seats this table actually has get shown, spread evenly so the
     * ring stays balanced whether there are three players or eight. */
    int shown = players;                 /* positions 1..players */
    int idx = 0;
    for (int pos = 1; pos <= MAX_POS; pos++) {
        if (pos > players) {
            lv_obj_add_flag(s_slot[pos], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        double ang = (-90.0 + (double)idx * 360.0 / (shown > 0 ? shown : 1))
                     * M_PI / 180.0;
        lv_obj_align(s_slot[pos], LV_ALIGN_CENTER,
                     (int)lround(RING_R * cos(ang)), (int)lround(RING_R * sin(ang)));
        idx++;

        bool used = taken[pos];
        lv_obj_set_style_bg_color(s_slot[pos],
            lv_color_hex(used ? UI_COL_BG : UI_COL_TILE), 0);
        lv_obj_set_style_border_width(s_slot[pos], used ? 1 : 2, 0);
        lv_obj_set_style_border_color(s_slot[pos],
            lv_color_hex(used ? UI_COL_MUTED : UI_COL_ACCENT), 0);
        lv_label_set_text_fmt(s_slot_lbl[pos], "%d", pos);
        lv_obj_set_style_text_color(s_slot_lbl[pos],
            lv_color_hex(used ? UI_COL_MUTED : UI_COL_TEXT), 0);
        /* A claimed seat stays visible (so the table is legible) but inert. */
        if (used) lv_obj_remove_flag(s_slot[pos], LV_OBJ_FLAG_CLICKABLE);
        else      lv_obj_add_flag(s_slot[pos], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_slot[pos], LV_OBJ_FLAG_HIDDEN);
    }

    nav_push(s_root, NULL);
}
