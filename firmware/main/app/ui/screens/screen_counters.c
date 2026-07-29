#include "screen_counters.h"

#include "counter.h"
#include "ui_common.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Every counter and token is one box in a scrollable 2-column grid. Add a new
 * one by dropping a counter_t in the table — the tiles and input routing adapt.
 * The first TOKEN_ROWS entries are "tokens" (a held count pops gold).
 */
static const char *const k_onoff[]    = { "OFF", "ON" };
static const char *const k_daynight[] = { "--", "Day", "Night" };
static const char *const k_ring[]     = { "--", "I", "II", "III", "IV" };

#define TOKEN_ROWS 4  /* Treasure/Food/Clue/Blood lead the table */

/* Commander-tax rows track times cast from the command zone; the tile shows the
 * derived tax (+2 per cast) rather than the raw count. Matched by name prefix. */
#define TAX_PER_CAST 2

static counter_t s_counters[] = {
    { .name = "Treasure",   .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Food",       .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Clue",       .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Blood",      .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 99, .wrap = false },
    /* Commander tax: value = times cast, displayed as +2 per cast. */
    { .name = "Cmdr A",     .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 30, .wrap = false },
    { .name = "Cmdr B",     .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 30, .wrap = false },
    { .name = "Poison",     .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 10, .wrap = false },
    { .name = "Energy",     .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Experience", .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 99, .wrap = false },
    /* Per-turn: zeroed by screen_counters_new_turn() (matched by name). */
    { .name = "Storm",      .type = COUNTER_TYPE_INT,    .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Monarch",    .type = COUNTER_TYPE_TOGGLE, .value = 0, .min = 0, .max = 1,  .wrap = true, .labels = k_onoff },
    { .name = "Initiative", .type = COUNTER_TYPE_TOGGLE, .value = 0, .min = 0, .max = 1,  .wrap = true, .labels = k_onoff },
    { .name = "Ring",       .type = COUNTER_TYPE_CYCLE,  .value = 0, .min = 0, .max = 4,  .wrap = true, .labels = k_ring },
    { .name = "Day/Night",  .type = COUNTER_TYPE_CYCLE,  .value = 0, .min = 0, .max = 2,  .wrap = true, .labels = k_daynight },
};
#define COUNTER_COUNT ((int)(sizeof(s_counters) / sizeof(s_counters[0])))

/* True for the commander-tax rows, which render their value specially. */
static bool is_tax_row(int i)
{
    return strncmp(s_counters[i].name, "Cmdr", 4) == 0;
}

static lv_obj_t *s_tile[COUNTER_COUNT];
static lv_obj_t *s_value[COUNTER_COUNT];
static lv_obj_t *s_flash[COUNTER_COUNT];
static int s_selected;

static void refresh_tile(int i)
{
    counter_t *c = &s_counters[i];
    char buf[12];
    if (is_tax_row(i)) {
        snprintf(buf, sizeof(buf), "+%d", c->value * TAX_PER_CAST);
    } else {
        counter_value_text(c, buf, sizeof(buf));
    }
    lv_label_set_text(s_value[i], buf);

    uint32_t col;
    if (is_tax_row(i)) {
        col = (c->value > 0) ? UI_COL_GOLD : UI_COL_MUTED;        /* commander tax */
    } else if (c->type != COUNTER_TYPE_INT) {
        col = (c->value > c->min) ? UI_COL_GOLD : UI_COL_MUTED;   /* active toggle/cycle */
    } else if (counter_at_max(c)) {
        col = UI_COL_DANGER;                                      /* e.g. poison 10 */
    } else if (i < TOKEN_ROWS && c->value > 0) {
        col = UI_COL_GOLD;                                        /* a held token pops */
    } else {
        col = (c->value > 0) ? UI_COL_TEXT : UI_COL_MUTED;
    }
    lv_obj_set_style_text_color(s_value[i], lv_color_hex(col), 0);
}

static void update_selection(void)
{
    for (int i = 0; i < COUNTER_COUNT; i++) {
        bool sel = (i == s_selected);
        lv_obj_set_style_border_width(s_tile[i], sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(s_tile[i],
                                      lv_color_hex(sel ? UI_COL_ACCENT : UI_COL_TILE), 0);
    }
}

/* Tap only SELECTS a counter (so an accidental tap while swiping between screens
 * can't change a value); the dial adjusts the selected one. */
static void tile_select_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_selected = i;
    update_selection();
}

lv_obj_t *screen_counters_create(void)
{
    lv_obj_t *scr = ui_make_round_screen();
    lv_obj_set_style_pad_hor(scr, 20, 0);
    /* A 3-wide row is 316 px, which only fits inside the round glass within
     * roughly +/-86 px of centre — so start the grid well down the face or the
     * top row is clipped by the bezel. */
    lv_obj_set_style_pad_top(scr, 58, 0);
    lv_obj_set_style_pad_bottom(scr, 44, 0);

    /* More boxes than fit the round face: scroll vertically. Horizontal swipes
     * still change screen (handled on the touch indev in app_main.c). */
    lv_obj_add_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    /* Three-column wrapping grid of tiles. */
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(scr, 8, 0);

    for (int i = 0; i < COUNTER_COUNT; i++) {
        lv_obj_t *tile = lv_obj_create(scr);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE); /* pass swipes to root */
        lv_obj_set_size(tile, 100, 80);   /* 3 across: 3*100 + 2*8 gap = 316 */
        lv_obj_set_style_bg_color(tile, lv_color_hex(UI_COL_TILE), 0);
        lv_obj_set_style_radius(tile, 14, 0);
        lv_obj_set_style_pad_all(tile, 3, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        s_tile[i] = tile;

        s_flash[i] = ui_make_flash_overlay(tile, UI_COL_ACCENT);
        lv_obj_set_style_radius(s_flash[i], 18, 0);

        lv_obj_t *name = lv_label_create(tile);
        lv_label_set_text(name, s_counters[i].name);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(UI_COL_MUTED), 0);
        /* Narrower tiles: ellipsize the long names rather than wrapping them
         * onto a second line and pushing the value out of the tile. */
        lv_obj_set_width(name, 92);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);

        s_value[i] = lv_label_create(tile);
        lv_obj_set_style_text_font(s_value[i], &lv_font_montserrat_28, 0);

        /* Tap the tile to select it; the dial adjusts the selected counter. */
        lv_obj_t *sel = ui_make_tap_zone(tile, tile_select_cb, (void *)(intptr_t)i);
        lv_obj_set_size(sel, lv_pct(100), lv_pct(100));
        lv_obj_center(sel);

        refresh_tile(i);
    }

    s_selected = 0;
    update_selection();
    return scr;
}

void screen_counters_reset(void)
{
    for (int i = 0; i < COUNTER_COUNT; i++) {
        s_counters[i].value = s_counters[i].min;
        refresh_tile(i);
    }
    s_selected = 0;
    update_selection();
}

int screen_counters_poison(void)
{
    for (int i = 0; i < COUNTER_COUNT; i++) {
        if (strcmp(s_counters[i].name, "Poison") == 0) return s_counters[i].value;
    }
    return 0;
}

int screen_counters_num(void)
{
    return COUNTER_COUNT;
}

int screen_counters_get_value(int i)
{
    return (i >= 0 && i < COUNTER_COUNT) ? s_counters[i].value : 0;
}

void screen_counters_set_value(int i, int v)
{
    if (i < 0 || i >= COUNTER_COUNT) return;
    counter_t *c = &s_counters[i];
    if (v < c->min) v = c->min;
    if (v > c->max) v = c->max;
    c->value = v;
    refresh_tile(i);
}

void screen_counters_new_turn(void)
{
    for (int i = 0; i < COUNTER_COUNT; i++) {
        if (strcmp(s_counters[i].name, "Storm") == 0) {
            s_counters[i].value = s_counters[i].min;
            refresh_tile(i);
        }
    }
}

void screen_counters_handle_input(input_event_t ev)
{
    switch (ev) {
    case INPUT_EV_SELECT_NEXT:
        s_selected = (s_selected + 1) % COUNTER_COUNT;
        update_selection();
        return;
    case INPUT_EV_SELECT_PREV:
        s_selected = (s_selected + COUNTER_COUNT - 1) % COUNTER_COUNT;
        update_selection();
        return;
    case INPUT_EV_INCREMENT:
    case INPUT_EV_ACTION: /* press advances toggles/cycles */
        counter_increment(&s_counters[s_selected]);
        break;
    case INPUT_EV_DECREMENT:
        counter_decrement(&s_counters[s_selected]);
        break;
    default:
        return;
    }

    refresh_tile(s_selected);
    ui_flash(s_flash[s_selected]);
}
