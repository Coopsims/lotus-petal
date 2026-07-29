#include "screen_pair.h"

#include "ui_common.h"
#include "nav.h"
#include "game.h"
#include "net_link.h"
#include "screen_mode.h"

#include <stdint.h>
#include <stdio.h>

typedef enum {
    VIEW_CHOICE = 0,  /* create or join?      */
    VIEW_JOIN,        /* dial in a 4-digit PIN */
    VIEW_LINKED,      /* PIN + who's connected */
} view_t;

#define DIGITS 4

static lv_obj_t *s_root;
static lv_obj_t *s_view_choice;
static lv_obj_t *s_view_join;
static lv_obj_t *s_view_linked;

static lv_obj_t *s_choice_btn[2];
static lv_obj_t *s_digit_box[DIGITS];
static lv_obj_t *s_digit_lbl[DIGITS];
static lv_obj_t *s_pin_lbl;
static lv_obj_t *s_linked_lbl;
static lv_obj_t *s_players_lbl;
static lv_obj_t *s_players_row;
static lv_obj_t *s_play_btn;
static lv_obj_t *s_play_lbl;
static lv_obj_t *s_err_lbl;

static view_t s_view;
static bool   s_link_only;   /* opened from Settings, not to start a game */
static int    s_choice_sel;
static int    s_digit[DIGITS];
static int    s_digit_sel;

static void show_view(view_t v)
{
    s_view = v;
    lv_obj_add_flag(s_view_choice, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_view_join,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_view_linked, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(v == VIEW_CHOICE ? s_view_choice
                       : v == VIEW_JOIN ? s_view_join
                                        : s_view_linked, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- choice view ---------- */

static void update_choice_highlight(void)
{
    for (int i = 0; i < 2; i++) {
        bool sel = (i == s_choice_sel);
        lv_obj_set_style_bg_color(s_choice_btn[i],
                                  lv_color_hex(sel ? UI_COL_ACCENT : UI_COL_TILE), 0);
        lv_obj_set_style_border_width(s_choice_btn[i], sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(s_choice_btn[i],
                                      lv_color_hex(sel ? UI_COL_TEXT : UI_COL_MUTED), 0);
    }
}

static void enter_linked(uint16_t pin)
{
    net_link_set_pin(pin);
    lv_label_set_text_fmt(s_pin_lbl, "%04u", (unsigned)pin);
    lv_label_set_text(s_linked_lbl, "searching...");
    show_view(VIEW_LINKED);
}

static void do_create(void *p)
{
    (void)p;
    if (!net_link_init()) {
        lv_label_set_text(s_err_lbl, LV_SYMBOL_WARNING "  radio failed");
        return;
    }
    enter_linked(net_link_new_pin());
}

static void do_join_view(void *p)
{
    (void)p;
    if (!net_link_init()) {
        lv_label_set_text(s_err_lbl, LV_SYMBOL_WARNING "  radio failed");
        return;
    }
    for (int i = 0; i < DIGITS; i++) s_digit[i] = 0;
    s_digit_sel = 0;
    for (int i = 0; i < DIGITS; i++) lv_label_set_text_fmt(s_digit_lbl[i], "%d", s_digit[i]);
    show_view(VIEW_JOIN);
}

static void choice_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_choice_sel = idx;
    update_choice_highlight();
    /* Deferred: bringing the radio up is slow, and swapping views inside the
     * touch dispatch is the LVGL re-entrancy that froze the device elsewhere. */
    lv_async_call(idx == 0 ? do_create : do_join_view, NULL);
}

/* ---------- join view ---------- */

static void update_digits(void)
{
    for (int i = 0; i < DIGITS; i++) {
        bool sel = (i == s_digit_sel);
        lv_label_set_text_fmt(s_digit_lbl[i], "%d", s_digit[i]);
        lv_obj_set_style_bg_color(s_digit_box[i],
                                  lv_color_hex(sel ? UI_COL_ACCENT : UI_COL_TILE), 0);
        lv_obj_set_style_border_width(s_digit_box[i], sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(s_digit_box[i],
                                      lv_color_hex(sel ? UI_COL_TEXT : UI_COL_MUTED), 0);
    }
}

static void digit_cb(lv_event_t *e)
{
    s_digit_sel = (int)(intptr_t)lv_event_get_user_data(e);
    update_digits();
}

static void connect_async(void *p)
{
    (void)p;
    uint16_t pin = (uint16_t)(s_digit[0] * 1000 + s_digit[1] * 100 +
                              s_digit[2] * 10 + s_digit[3]);
    enter_linked(pin);
}

/* Out of PIN entry, back to create-or-join. */
static void join_back_async(void *p) { (void)p; show_view(VIEW_CHOICE); }
static void join_back_cb(lv_event_t *e) { (void)e; lv_async_call(join_back_async, NULL); }

/* Out of pairing altogether, back to Local / Remote. */
static void choice_back_async(void *p)
{
    (void)p;
    nav_pop();
    if (!s_link_only) screen_mode_open();
}
static void choice_back_cb(lv_event_t *e) { (void)e; lv_async_call(choice_back_async, NULL); }

static void connect_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(connect_async, NULL);
}

/* ---------- linked view ---------- */

/* Table size is a shared setting, not a head count: some players may be at the
 * table without a dial of their own. */
static void players_step_cb(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    game_set_total_players(game_total_players() + step);
    screen_pair_tick();
}

static void play_async(void *p)
{
    (void)p;
    /* Linking from Settings should hand you back to Settings, not drop you into
     * a game you never asked to start. */
    if (s_link_only) nav_pop();
    else             nav_pop_all();
}

static void play_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(play_async, NULL);
}

static void leave_async(void *p)
{
    (void)p;
    net_link_leave();
    if (!s_link_only) game_set_mode(GAME_MODE_LOCAL);
    show_view(VIEW_CHOICE);
}

static void leave_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(leave_async, NULL);
}

/* ---------- dial ---------- */

static void pair_enc(int delta)
{
    if (s_view == VIEW_CHOICE) {
        s_choice_sel = ((s_choice_sel + delta) % 2 + 2) % 2;
        update_choice_highlight();
    } else if (s_view == VIEW_JOIN) {
        int *d = &s_digit[s_digit_sel];
        *d = ((*d + delta) % 10 + 10) % 10;
        update_digits();
    }
}

/* ---------- build ---------- */

static lv_obj_t *make_pill(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                           void *ud, uint32_t colour)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 190, 46);
    lv_obj_set_style_radius(b, 23, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *make_column(lv_obj_t *parent, int pad_row)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, UI_DIM, UI_DIM);
    lv_obj_center(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, pad_row, 0);
    return c;
}

void screen_pair_init(void)
{
    s_root = ui_make_round_screen();

    /* --- choice --- */
    s_view_choice = make_column(s_root, 14);

    lv_obj_t *t1 = lv_label_create(s_view_choice);
    lv_label_set_text(t1, LV_SYMBOL_WIFI "  REMOTE");
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(t1, lv_color_hex(UI_COL_TEXT), 0);

    s_choice_btn[0] = make_pill(s_view_choice, "Create game", choice_cb,
                                (void *)(intptr_t)0, UI_COL_TEXT);
    s_choice_btn[1] = make_pill(s_view_choice, "Join game", choice_cb,
                                (void *)(intptr_t)1, UI_COL_TEXT);

    lv_obj_t *cback = ui_make_back_button(s_root, choice_back_cb);
    lv_obj_align(cback, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_move_foreground(cback);

    s_err_lbl = lv_label_create(s_view_choice);
    lv_label_set_text(s_err_lbl, "");
    lv_obj_set_style_text_font(s_err_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_err_lbl, lv_color_hex(UI_COL_DANGER), 0);

    /* --- join (PIN entry) --- */
    s_view_join = make_column(s_root, 10);

    lv_obj_t *t2 = lv_label_create(s_view_join);
    lv_label_set_text(t2, "ENTER PIN");
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t2, lv_color_hex(UI_COL_TEXT), 0);

    lv_obj_t *row = lv_obj_create(s_view_join);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, DIGITS * 56 + (DIGITS - 1) * 8, LV_SIZE_CONTENT);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    for (int i = 0; i < DIGITS; i++) {
        lv_obj_t *box = lv_button_create(row);
        lv_obj_set_size(box, 56, 70);
        lv_obj_set_style_radius(box, 12, 0);
        lv_obj_set_style_shadow_width(box, 0, 0);
        lv_obj_add_event_cb(box, digit_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_digit_box[i] = box;

        lv_obj_t *l = lv_label_create(box);
        lv_label_set_text(l, "0");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_TEXT), 0);
        lv_obj_center(l);
        s_digit_lbl[i] = l;
    }

    lv_obj_t *hint = lv_label_create(s_view_join);
    lv_label_set_text(hint, "tap a digit, dial to set");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COL_MUTED), 0);

    make_pill(s_view_join, LV_SYMBOL_OK "  Connect", connect_cb, NULL, UI_COL_TEXT);
    make_pill(s_view_join, LV_SYMBOL_LEFT "  Back", join_back_cb, NULL, UI_COL_MUTED);

    /* --- linked --- */
    s_view_linked = make_column(s_root, 6);

    lv_obj_t *t3 = lv_label_create(s_view_linked);
    lv_label_set_text(t3, "GAME PIN");
    lv_obj_set_style_text_font(t3, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(t3, lv_color_hex(UI_COL_MUTED), 0);

    s_pin_lbl = lv_label_create(s_view_linked);
    lv_label_set_text(s_pin_lbl, "----");
    lv_obj_set_style_text_font(s_pin_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_pin_lbl, lv_color_hex(UI_COL_GOLD), 0);

    s_linked_lbl = lv_label_create(s_view_linked);
    lv_label_set_text(s_linked_lbl, "searching...");
    lv_obj_set_style_text_font(s_linked_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_linked_lbl, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_pad_bottom(s_linked_lbl, 6, 0);

    /* Players at the table (>= the number of dials linked). */
    lv_obj_t *prow = lv_obj_create(s_view_linked);
    s_players_row = prow;
    lv_obj_remove_style_all(prow);
    lv_obj_set_size(prow, 200, LV_SIZE_CONTENT);
    lv_obj_remove_flag(prow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *pminus = lv_button_create(prow);
    lv_obj_set_size(pminus, 42, 38);
    lv_obj_set_style_radius(pminus, 19, 0);
    lv_obj_set_style_bg_color(pminus, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(pminus, 0, 0);
    lv_obj_add_event_cb(pminus, players_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *pm = lv_label_create(pminus);
    lv_label_set_text(pm, LV_SYMBOL_MINUS);
    lv_obj_center(pm);

    s_players_lbl = lv_label_create(prow);
    lv_label_set_text(s_players_lbl, "2 players");
    lv_obj_set_style_text_font(s_players_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_players_lbl, lv_color_hex(UI_COL_TEXT), 0);

    lv_obj_t *pplus = lv_button_create(prow);
    lv_obj_set_size(pplus, 42, 38);
    lv_obj_set_style_radius(pplus, 19, 0);
    lv_obj_set_style_bg_color(pplus, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(pplus, 0, 0);
    lv_obj_add_event_cb(pplus, players_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *pp = lv_label_create(pplus);
    lv_label_set_text(pp, LV_SYMBOL_PLUS);
    lv_obj_center(pp);

    s_play_btn = make_pill(s_view_linked, LV_SYMBOL_PLAY "  Play", play_cb, NULL, UI_COL_TEXT);
    s_play_lbl = lv_obj_get_child(s_play_btn, 0);
    make_pill(s_view_linked, LV_SYMBOL_CLOSE "  Leave", leave_cb, NULL, UI_COL_MUTED);

    show_view(VIEW_CHOICE);
}

/* Shared entry: `link_only` skips everything to do with running a table. */
static void open_common(bool link_only)
{
    s_link_only  = link_only;
    s_choice_sel = 0;
    lv_label_set_text(s_err_lbl, "");
    update_choice_highlight();
    show_view(VIEW_CHOICE);

    if (s_players_row) {
        if (link_only) lv_obj_add_flag(s_players_row, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_remove_flag(s_players_row, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_play_lbl) {
        lv_label_set_text(s_play_lbl, link_only ? LV_SYMBOL_OK "  Done"
                                                : LV_SYMBOL_PLAY "  Play");
    }
    nav_push(s_root, pair_enc);
}

void screen_pair_open_link(void)
{
    open_common(true);
}

void screen_pair_open(void)
{
    open_common(false);
}



void screen_pair_tick(void)
{
    if (s_view != VIEW_LINKED || !s_linked_lbl) return;

    if (s_players_lbl) {
        int total = game_total_players();
        int dials = net_link_member_count();
        if (total > dials) {
            lv_label_set_text_fmt(s_players_lbl, "%d players (%d dials)", total, dials);
        } else {
            lv_label_set_text_fmt(s_players_lbl, "%d players", total);
        }
    }

    int others = net_link_member_count() - 1;
    if (others <= 0) {
        lv_label_set_text(s_linked_lbl, "searching...");
        lv_obj_set_style_text_color(s_linked_lbl, lv_color_hex(UI_COL_MUTED), 0);
    } else {
        lv_label_set_text_fmt(s_linked_lbl, "%d dial%s linked",
                              others, others == 1 ? "" : "s");
        lv_obj_set_style_text_color(s_linked_lbl, lv_color_hex(UI_COL_GOLD), 0);
    }
}
