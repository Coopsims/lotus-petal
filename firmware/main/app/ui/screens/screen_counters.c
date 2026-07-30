#include "screen_counters.h"

#include "counter.h"
#include "game.h"
#include "ui_common.h"

#include <stdint.h>
#include <stdio.h>

/*
 * Every counter and token is one box in a scrollable grid. Add a new one by
 * dropping a counter_t in the table below — the tiles, the input routing and the
 * persistence all size themselves from it.
 *
 * Anything special about a counter is declared in its `role` and `per_turn`
 * fields rather than inferred from its name or its position in the table. That
 * matters: this screen used to find poison with strcmp(name, "Poison") and the
 * tokens by "the first four rows", so renaming a label or reordering the table
 * silently changed behaviour — including the lethal-threshold check that decides
 * whether the bow-out button appears at all.
 *
 * NOTE: saved games are stored by table INDEX, so inserting a counter anywhere
 * but the end shifts existing saves. Bump PERSIST_VERSION in model/persist.c if
 * you reorder, and old saves are then discarded cleanly instead of misread.
 */
static const char *const k_onoff[]    = { "OFF", "ON" };
static const char *const k_daynight[] = { "--", "Day", "Night" };
static const char *const k_ring[]     = { "--", "I", "II", "III", "IV" };

static counter_t s_counters[] = {
    { .name = "Treasure",   .type = COUNTER_TYPE_INT,    .role = COUNTER_ROLE_TOKEN,         .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Food",       .type = COUNTER_TYPE_INT,    .role = COUNTER_ROLE_TOKEN,         .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Clue",       .type = COUNTER_TYPE_INT,    .role = COUNTER_ROLE_TOKEN,         .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Blood",      .type = COUNTER_TYPE_INT,    .role = COUNTER_ROLE_TOKEN,         .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Cmdr A",     .type = COUNTER_TYPE_INT,    .role = COUNTER_ROLE_COMMANDER_TAX, .value = 0, .min = 0, .max = 30, .wrap = false },
    { .name = "Cmdr B",     .type = COUNTER_TYPE_INT,    .role = COUNTER_ROLE_COMMANDER_TAX, .value = 0, .min = 0, .max = 30, .wrap = false },
    { .name = "Poison",     .type = COUNTER_TYPE_INT,    .role = COUNTER_ROLE_POISON,        .value = 0, .min = 0, .max = COUNTER_POISON_LETHAL, .wrap = false },
    { .name = "Energy",     .type = COUNTER_TYPE_INT,                                        .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Experience", .type = COUNTER_TYPE_INT,                                        .value = 0, .min = 0, .max = 99, .wrap = false },
    { .name = "Storm",      .type = COUNTER_TYPE_INT,                                        .value = 0, .min = 0, .max = 99, .wrap = false, .per_turn = true },
    { .name = "Monarch",    .type = COUNTER_TYPE_TOGGLE,  .role = COUNTER_ROLE_MONARCH,      .value = 0, .min = 0, .max = 1,  .wrap = true, .labels = k_onoff },
    { .name = "Initiative", .type = COUNTER_TYPE_TOGGLE,  .role = COUNTER_ROLE_INITIATIVE,   .value = 0, .min = 0, .max = 1,  .wrap = true, .labels = k_onoff },
    { .name = "Ring",       .type = COUNTER_TYPE_CYCLE,                                      .value = 0, .min = 0, .max = 4,  .wrap = true, .labels = k_ring },
    { .name = "Day/Night",  .type = COUNTER_TYPE_CYCLE,                                      .value = 0, .min = 0, .max = 2,  .wrap = true, .labels = k_daynight },
};
#define COUNTER_COUNT ((int)(sizeof(s_counters) / sizeof(s_counters[0])))

static lv_obj_t *s_tile[COUNTER_COUNT];
static lv_obj_t *s_value[COUNTER_COUNT];
static lv_obj_t *s_flash[COUNTER_COUNT];
static int s_selected;

/* Which turn position currently holds this table-wide counter, or 0 for nobody.
 * Only meaningful in a linked game. */
static int table_holder(const counter_t *c)
{
    if (c->role == COUNTER_ROLE_MONARCH)    return game_monarch();
    if (c->role == COUNTER_ROLE_INITIATIVE) return game_initiative();
    return 0;
}

/* Claim or release a table-wide counter for this dial. */
static void table_claim(const counter_t *c, bool mine)
{
    int pos = mine ? game_my_position() : 0;
    if (c->role == COUNTER_ROLE_MONARCH)    game_set_monarch(pos);
    if (c->role == COUNTER_ROLE_INITIATIVE) game_set_initiative(pos);
}

/* True when the table decides this counter rather than this dial: a linked game
 * with seats settled far enough that positions mean something. */
static bool table_wide_live(const counter_t *c)
{
    return counter_role_is_table_wide(c->role) &&
           game_mode() == GAME_MODE_REMOTE &&
           game_my_position() > 0;
}

static void refresh_tile(int i)
{
    counter_t *c = &s_counters[i];

    /* Role-derived rendering (commander tax) is handled inside the model, so
     * every counter draws through the same call. */
    char buf[12];
    counter_value_text(c, buf, sizeof(buf));

    /* Someone else holding the crown is more useful than being told "OFF": show
     * whose it is, using the same P<n> turn positions the rest of the UI does. */
    if (table_wide_live(c)) {
        int holder = table_holder(c);
        if (holder > 0 && holder != game_my_position()) {
            snprintf(buf, sizeof(buf), "P%d", holder);
        }
    }
    lv_label_set_text(s_value[i], buf);

    uint32_t col;
    if (c->role == COUNTER_ROLE_COMMANDER_TAX) {
        col = (c->value > 0) ? UI_COL_GOLD : UI_COL_MUTED;        /* tax is owed */
    } else if (c->type != COUNTER_TYPE_INT) {
        col = (c->value > c->min) ? UI_COL_GOLD : UI_COL_MUTED;   /* active toggle/cycle */
    } else if (counter_at_max(c)) {
        col = UI_COL_DANGER;                                      /* e.g. lethal poison */
    } else if (c->role == COUNTER_ROLE_TOKEN && c->value > 0) {
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
        counter_reset(&s_counters[i]);
        refresh_tile(i);
    }
    s_selected = 0;
    update_selection();
}

int screen_counters_poison(void)
{
    for (int i = 0; i < COUNTER_COUNT; i++) {
        if (s_counters[i].role == COUNTER_ROLE_POISON) return s_counters[i].value;
    }
    return 0;   /* a build with no poison counter simply never reaches that threshold */
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

void screen_counters_sync_shared(void)
{
    for (int i = 0; i < COUNTER_COUNT; i++) {
        counter_t *c = &s_counters[i];
        if (!table_wide_live(c)) continue;

        /* Exactly one player holds it, so ours is on only while the table says the
         * holder is us. That is what makes a second dial claiming it turn ours off
         * without either dial having to ask the other. */
        int want = (table_holder(c) == game_my_position()) ? c->max : c->min;
        if (c->value != want) {
            c->value = want;
            refresh_tile(i);
        } else {
            refresh_tile(i);   /* the holder shown may still have changed */
        }
    }
}

void screen_counters_new_turn(void)
{
    for (int i = 0; i < COUNTER_COUNT; i++) {
        if (!s_counters[i].per_turn) continue;
        counter_reset(&s_counters[i]);
        refresh_tile(i);
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

    /* For a table-wide counter the local value is only the input; the table is
     * the truth. Publish the claim and let the next sync settle what is shown —
     * including taking the crown off whoever had it. */
    counter_t *changed = &s_counters[s_selected];
    if (table_wide_live(changed)) table_claim(changed, changed->value > changed->min);

    refresh_tile(s_selected);
    ui_flash(s_flash[s_selected]);
}
