#include "screen_life.h"

#include "counter.h"
#include "ui_common.h"
#include "screen_tools.h"
#include "screen_commander.h"
#include "screen_counters.h"
#include "screen_dice.h"
#include "screen_order.h"
#include "game.h"
#include "net_link.h"
#include "app.h"

#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Radius of the linked-dial ring. The life arc is 336 across with an 18px
 * stroke, so its inner edge sits at 150 from centre; a 68px gauge at r=116 met
 * it exactly. Pulled in to leave clear air between the two, and it still clears
 * the battery readout at the top. */
#define PEER_R 104

/* Geometry of the life gauge, shared by the base ring and the overflow ring. */
#define LIFE_RING_D 336
#define LIFE_RING_W 18

/* The base ring reads "full" at the starting life and empties as life falls to 0. */
#define LIFE_BAR_FULL GAME_STARTING_LIFE

/* Above the starting total the gauge has nowhere left to grow, so a SECOND ring
 * fills from the opposite end and eats the base ring as life climbs — gaining
 * life keeps changing the picture instead of pegging at full. Blue at one over,
 * purple by the time the second ring is full at double the starting total. */
#define LIFE_OVER_HUE_LO 220   /* blue   */
#define LIFE_OVER_HUE_HI 285   /* purple */

/* Past double there is no length left to say anything with, so the colour cycles
 * instead. The timer only runs while it is needed. */
#define RAINBOW_PERIOD_MS 70
#define RAINBOW_STEP_DEG  6

static counter_t s_life = {
    .name = "Life",
    .type = COUNTER_TYPE_INT,
    .value = GAME_STARTING_LIFE,
    .min = -99,
    .max = 999,
    .wrap = false,
};

static lv_obj_t *s_number;
static lv_obj_t *s_arc;
static lv_obj_t *s_seatlbl;      /* "P2" — which seat this dial is */
static lv_obj_t *s_turnlbl;      /* "TURN n" above the life total */
static lv_obj_t *s_passbtn_lbl;
static lv_obj_t *s_batt;
static lv_obj_t *s_poison;   /* only shown once you actually have some */
static lv_obj_t *s_skull;    /* bow-out button, offered at a lethal threshold */
static int s_batt_anim;
static lv_obj_t *s_peer[NET_MAX_OTHERS];      /* small life ring per linked dial */
static lv_obj_t *s_peer_name[NET_MAX_OTHERS]; /* "P2" — goes gold on their turn  */
static lv_obj_t *s_peer_life[NET_MAX_OTHERS]; /* their life, always high contrast */

static lv_obj_t   *s_over;      /* overflow ring, above the starting total */
static lv_timer_t *s_rainbow;   /* runs only past double the starting total */
static uint16_t    s_rainbow_hue;

static void refresh(void);
static void refresh_turn(void);
static void refresh_overflow(void);

/* Draw one rim readout. `m` is NULL for a player at the table with no dial —
 * their seat still shows, with a dash where the life total would be.
 * `label_num` is a TURN POSITION, the same number shown on the seat picker and
 * the commander wedges; 0 means that dial has not picked a seat yet, which is
 * shown as "P?" rather than a number that would later change. */
static void place_peer(int i, int ox, int oy, int label_num,
                       const net_member_t *m, bool is_active)
{
    if (label_num > 0) lv_label_set_text_fmt(s_peer_name[i], "P%d", label_num);
    else               lv_label_set_text(s_peer_name[i], "P?");
    lv_obj_set_style_text_color(s_peer_name[i],
        lv_color_hex(is_active ? UI_COL_GOLD : UI_COL_MUTED), 0);

    if (m) {
        bool dead = !m->alive || m->life <= 0;
        lv_label_set_text_fmt(s_peer_life[i], "%d", m->life);
        lv_obj_set_style_text_color(s_peer_life[i],
            lv_color_hex(dead ? UI_COL_DANGER : UI_COL_TEXT), 0);

        int pbar = m->life;
        if (pbar < 0) pbar = 0;
        if (pbar > LIFE_BAR_FULL) pbar = LIFE_BAR_FULL;
        lv_arc_set_value(s_peer[i], pbar);
        uint16_t phue = (uint16_t)((long)pbar * 120 / LIFE_BAR_FULL);
        lv_obj_set_style_arc_color(s_peer[i], lv_color_hsv_to_rgb(phue, 85, 95),
                                   LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(s_peer[i], LV_OPA_COVER, LV_PART_INDICATOR);
    } else {
        /* No dial to report a life total, so show the seat and leave the gauge
         * empty rather than implying a number we do not have. */
        lv_label_set_text(s_peer_life[i], "-");
        lv_obj_set_style_text_color(s_peer_life[i], lv_color_hex(UI_COL_MUTED), 0);
        lv_arc_set_value(s_peer[i], 0);
        lv_obj_set_style_arc_opa(s_peer[i], LV_OPA_TRANSP, LV_PART_INDICATOR);
    }

    lv_obj_align(s_peer[i], LV_ALIGN_CENTER, ox, oy);
    lv_obj_remove_flag(s_peer[i], LV_OBJ_FLAG_HIDDEN);
}

void screen_life_refresh_peers(void)
{
    if (!s_peer[0]) return;

    /* The turn (and so who owns the gold) can change on another dial, so both
     * follow the network tick rather than only local input. */
    refresh_turn();
    refresh();

    /* Local game: no rim, nothing to show. */
    if (game_mode() != GAME_MODE_REMOTE) {
        for (int i = 0; i < NET_MAX_OTHERS; i++) {
            lv_obj_add_flag(s_peer[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    net_member_t others[NET_MAX_OTHERS];
    int n = net_link_others(others, NET_MAX_OTHERS);
    int players = game_total_players();
    int my_pos  = net_link_my_order();
    int active  = net_link_active_pos();

    int shown = 0;
    if (my_pos > 0) {
        /* Seated: lay the whole table out by turn position, so a player without
         * a dial still occupies their place in the circle. */
        for (int pos = 1; pos <= players && shown < NET_MAX_OTHERS; pos++) {
            if (pos == my_pos) continue;

            const net_member_t *m = NULL;
            for (int i = 0; i < n; i++) {
                if (others[i].order == pos) { m = &others[i]; break; }
            }

            int slot = ((pos - my_pos) % players + players) % players;
            double ang = (90.0 + (double)slot * 360.0 / (double)players) * M_PI / 180.0;
            place_peer(shown, (int)lround(PEER_R * cos(ang)),
                              (int)lround(PEER_R * sin(ang)),
                       pos, m, pos == active);
            shown++;
        }
    } else {
        /* We have not picked a seat yet, so there is no ordering to lay out by.
         * Show who is linked, spaced evenly, labelled by whatever position they
         * have claimed — never by the internal seat index, which is a MAC
         * ordering the player never sees anywhere else. */
        int seats = n + 1;
        int self  = net_link_self_seat();
        for (int i = 0; i < n && shown < NET_MAX_OTHERS; i++) {
            int slot = ((others[i].seat - self) % seats + seats) % seats;
            double ang = (90.0 + (double)slot * 360.0 / (double)seats) * M_PI / 180.0;
            place_peer(shown, (int)lround(PEER_R * cos(ang)),
                              (int)lround(PEER_R * sin(ang)),
                       others[i].order, &others[i],
                       others[i].order > 0 && others[i].order == active);
            shown++;
        }
    }

    for (int i = shown; i < NET_MAX_OTHERS; i++) {
        lv_obj_add_flag(s_peer[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void screen_life_set_battery(int pct, bool charging)
{
    if (!s_batt) return;

    /* No battery on this board: better no gauge than a gauge stuck at "--". */
    if (pct < 0 && !charging) {
        lv_obj_add_flag(s_batt, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_batt, LV_OBJ_FLAG_HIDDEN);

    if (charging) {
        /* We can't read the cell's charge level while plugged (the ADC senses
         * the held-high rail), so animate a filling battery + bolt instead of a
         * static 100 %. One frame advances per call. */
        static const char *const fill[] = {
            LV_SYMBOL_BATTERY_EMPTY, LV_SYMBOL_BATTERY_1, LV_SYMBOL_BATTERY_2,
            LV_SYMBOL_BATTERY_3, LV_SYMBOL_BATTERY_FULL,
        };
        s_batt_anim = (s_batt_anim + 1) % 5;
        lv_label_set_text_fmt(s_batt, "%s " LV_SYMBOL_CHARGE, fill[s_batt_anim]);
        lv_obj_set_style_text_color(s_batt, lv_color_hex(UI_COL_GOLD), 0);
        return;
    }

    s_batt_anim = 0;
    const char *glyph = LV_SYMBOL_BATTERY_FULL;
    if (pct <= 10)      glyph = LV_SYMBOL_BATTERY_EMPTY;
    else if (pct <= 35) glyph = LV_SYMBOL_BATTERY_1;
    else if (pct <= 60) glyph = LV_SYMBOL_BATTERY_2;
    else if (pct <= 85) glyph = LV_SYMBOL_BATTERY_3;
    lv_label_set_text_fmt(s_batt, "%s %d%%", glyph, pct);
    lv_obj_set_style_text_color(s_batt,
                                lv_color_hex(pct <= 15 ? UI_COL_DANGER : UI_COL_MUTED), 0);
}

/* Turn readout sits above the life total; the button below just passes. While a
 * game still needs a first player the readout becomes a button that opens the
 * roller, so the prompt is also the way to act on it. */
static void refresh_turn(void)
{
    /* Which seat this dial is. Only meaningful once dials are linked. */
    if (game_mode() == GAME_MODE_REMOTE) {
        lv_label_set_text_fmt(s_seatlbl, "P%d", net_link_self_seat() + 1);
        lv_obj_set_style_text_color(s_seatlbl,
            lv_color_hex(game_is_my_turn() ? UI_COL_GOLD : UI_COL_MUTED), 0);
        lv_obj_remove_flag(s_seatlbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_seatlbl, LV_OBJ_FLAG_HIDDEN);
    }

    first_status_t st = game_first_status();
    bool tappable = (st == FIRST_NEED_ROLL || st == FIRST_TIE || st == FIRST_PICK_SEAT);

    switch (st) {
    case FIRST_NEED_ROLL:
        lv_label_set_text(s_turnlbl, "ROLL D20 " LV_SYMBOL_SHUFFLE);
        break;
    case FIRST_WAITING:
        lv_label_set_text(s_turnlbl, "WAITING...");
        break;
    case FIRST_TIE:
        lv_label_set_text(s_turnlbl, "TIE - ROLL AGAIN");
        break;
    case FIRST_PICK_SEAT:
        lv_label_set_text(s_turnlbl, "PICK SEAT " LV_SYMBOL_RIGHT);
        break;
    case FIRST_WAIT_SEATS:
        lv_label_set_text(s_turnlbl, "SEATING...");
        break;
    default:
        lv_label_set_text_fmt(s_turnlbl, "TURN %d", game_turn());
        break;
    }

    if (tappable) lv_obj_add_flag(s_turnlbl, LV_OBJ_FLAG_CLICKABLE);
    else          lv_obj_remove_flag(s_turnlbl, LV_OBJ_FLAG_CLICKABLE);

    uint32_t col = (st == FIRST_TIE)     ? UI_COL_DANGER
                 : tappable              ? UI_COL_ACCENT
                 : game_is_my_turn()     ? UI_COL_GOLD
                                         : UI_COL_MUTED;
    lv_obj_set_style_text_color(s_turnlbl, lv_color_hex(col), 0);
}

/* Cycles the overflow ring's hue. Only resumed past double the starting total,
 * where the ring is already full and length can no longer convey anything. */
static void rainbow_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    s_rainbow_hue = (uint16_t)((s_rainbow_hue + RAINBOW_STEP_DEG) % 360);
    lv_obj_set_style_arc_color(s_over, lv_color_hsv_to_rgb(s_rainbow_hue, 90, 100),
                               LV_PART_INDICATOR);
}

/* Life above the starting total. Grows from the far end of the gauge (REVERSE),
 * so it covers the green rather than extending past it, and sweeps blue -> purple
 * as it fills. */
static void refresh_overflow(void)
{
    if (!s_over) return;

    int over = s_life.value - LIFE_BAR_FULL;
    if (over <= 0) {
        lv_obj_add_flag(s_over, LV_OBJ_FLAG_HIDDEN);
        if (s_rainbow) lv_timer_pause(s_rainbow);
        return;
    }
    lv_obj_remove_flag(s_over, LV_OBJ_FLAG_HIDDEN);

    int len = (over > LIFE_BAR_FULL) ? LIFE_BAR_FULL : over;
    lv_arc_set_value(s_over, len);

    if (over > LIFE_BAR_FULL) {
        if (s_rainbow) lv_timer_resume(s_rainbow);   /* the colour does the talking */
        return;
    }

    if (s_rainbow) lv_timer_pause(s_rainbow);
    uint16_t hue = (uint16_t)(LIFE_OVER_HUE_LO +
        (long)len * (LIFE_OVER_HUE_HI - LIFE_OVER_HUE_LO) / LIFE_BAR_FULL);
    lv_obj_set_style_arc_color(s_over, lv_color_hsv_to_rgb(hue, 85, 95),
                               LV_PART_INDICATOR);
}

static void refresh(void)
{
    char buf[8];
    counter_value_text(&s_life, buf, sizeof(buf));
    lv_label_set_text(s_number, buf);

    /* Danger still wins over everything; otherwise the total goes gold while the
     * turn is ours, so a glance at the table shows who is up. */
    uint32_t col = game_is_eliminated()  ? UI_COL_MUTED
                 : (s_life.value <= 5)  ? UI_COL_DANGER
                 : game_is_my_turn()    ? UI_COL_GOLD
                                        : UI_COL_TEXT;
    lv_obj_set_style_text_color(s_number, lv_color_hex(col), 0);

    int psn = screen_counters_poison();
    if (psn > 0) {
        lv_label_set_text_fmt(s_poison, "%d psn", psn);
        lv_obj_set_style_text_color(s_poison,
            lv_color_hex(psn >= 10 ? UI_COL_DANGER : 0x22C55E), 0);
        lv_obj_remove_flag(s_poison, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_poison, LV_OBJ_FLAG_HIDDEN);
    }

    bool out = game_is_eliminated();
    if (out || game_can_eliminate()) {
        lv_obj_set_style_bg_color(s_skull,
            lv_color_hex(out ? UI_COL_DANGER : UI_COL_TILE), 0);
        lv_obj_set_style_border_color(s_skull,
            lv_color_hex(out ? UI_COL_TEXT : UI_COL_DANGER), 0);
        lv_obj_remove_flag(s_skull, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_skull, LV_OBJ_FLAG_HIDDEN);
    }

    /* Base ring: length tracks life 0..LIFE_BAR_FULL, colour sweeps green (full)
     * through yellow to red (near 0) via hue 120..0. Above the starting total it
     * simply sits full and the overflow ring takes over. */
    int bar = s_life.value;
    if (bar < 0) {
        bar = 0;
    } else if (bar > LIFE_BAR_FULL) {
        bar = LIFE_BAR_FULL;
    }
    lv_arc_set_value(s_arc, bar);

    uint16_t hue = (uint16_t)((long)bar * 120 / LIFE_BAR_FULL);
    lv_obj_set_style_arc_color(s_arc, lv_color_hsv_to_rgb(hue, 85, 95),
                               LV_PART_INDICATOR);

    refresh_overflow();
}

static void menu_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    screen_tools_open(); /* holding the middle of the face opens the Tools screen */
}

/* A short tap anywhere on the face opens the Commander-damage screen (which is
 * linked back to this life total). Uses SHORT_CLICKED so a hold-for-Tools never
 * also fires it, and ignores swipes so they still reach the Counters screen. */
static void commander_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!app_touch_release_was_tap()) return; /* a swipe, not a tap — let it navigate */
    screen_commander_open();
}

static void open_roller_async(void *p)
{
    LV_UNUSED(p);
    screen_dice_open_first();
}

static void open_order_async(void *p)
{
    LV_UNUSED(p);
    screen_order_open();
}

/* Tapping "ROLL FOR 1ST" opens the roller with the first-player die selected,
 * so the dial can be spun straight away instead of digging through Tools. */
static void roll_first_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    first_status_t st = game_first_status();
    if (st == FIRST_PICK_SEAT) { lv_async_call(open_order_async, NULL); return; }
    if (st != FIRST_NEED_ROLL && st != FIRST_TIE) return;
    lv_async_call(open_roller_async, NULL);
}

/* Reaching zero life does not mean you are out — life dips below zero all the
 * time mid-resolution — so this only ever offers the choice. Tapping again
 * undoes it, because it is one tap away from being a misclick. */
static void skull_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    game_set_eliminated(!game_is_eliminated());
    refresh_turn();
    refresh();
}

static void pass_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    game_pass_turn(); /* hand the turn on (locally: just the next turn) */
    refresh_turn();
    refresh();
}

/* Re-read the turn when the screen comes back into view (e.g. after Undo Turn on
 * the Tools overlay changed it). */
static void screen_loaded_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    refresh_turn();
}

void screen_life_reset(void)
{
    s_life.value = GAME_STARTING_LIFE;
    refresh();
}

void screen_life_apply_delta(int delta)
{
    s_life.value += delta;
    if (s_life.value < s_life.min) s_life.value = s_life.min;
    if (s_life.value > s_life.max) s_life.value = s_life.max;
    refresh();
}

int screen_life_get_life(void)
{
    return s_life.value;
}

void screen_life_set_life(int v)
{
    if (v < s_life.min) v = s_life.min;
    if (v > s_life.max) v = s_life.max;
    s_life.value = v;
    refresh();
}

lv_obj_t *screen_life_create(void)
{
    lv_obj_t *scr = ui_make_round_screen();

    /* Life ring hugging the bezel: a 270° gauge (gap at the bottom) that
     * shrinks and shifts green -> red as life falls. Purely decorative, so it
     * floats above the layout and never intercepts taps. */
    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, LIFE_RING_D, LIFE_RING_D);
    lv_obj_center(s_arc);
    lv_obj_add_flag(s_arc, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB); /* hide the drag knob */
    lv_arc_set_bg_angles(s_arc, 135, 45);           /* 270°, opening downward */
    lv_arc_set_range(s_arc, 0, LIFE_BAR_FULL);
    lv_obj_set_style_arc_width(s_arc, LIFE_RING_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, LIFE_RING_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(UI_COL_TILE), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

    /* Overflow ring, on the same track and created straight after the base ring
     * so it draws on top of it. REVERSE grows it from the opposite end, and its
     * own background is transparent — a painted track would hide the very green
     * it is supposed to be covering over. */
    s_over = lv_arc_create(scr);
    lv_obj_set_size(s_over, LIFE_RING_D, LIFE_RING_D);
    lv_obj_center(s_over);
    lv_obj_add_flag(s_over, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_over, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_over, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(s_over, NULL, LV_PART_KNOB);
    lv_arc_set_bg_angles(s_over, 135, 45);
    lv_arc_set_mode(s_over, LV_ARC_MODE_REVERSE);
    lv_arc_set_range(s_over, 0, LIFE_BAR_FULL);
    lv_obj_set_style_arc_width(s_over, LIFE_RING_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_over, LIFE_RING_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_over, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_over, true, LV_PART_INDICATOR);

    s_rainbow = lv_timer_create(rainbow_cb, RAINBOW_PERIOD_MS, NULL);
    lv_timer_pause(s_rainbow);

    /* Full-face tap target for opening Commander damage. Created early so it sits
     * BELOW the centre menu and Turn button (which catch their own taps); a short
     * tap on any other part of the face opens Commander. SHORT_CLICKED keeps a
     * hold-for-Tools from also triggering it, and swipes travel too far to click
     * (they bubble up for navigation). */
    lv_obj_t *tapz = lv_obj_create(scr);
    lv_obj_remove_style_all(tapz);
    lv_obj_set_size(tapz, UI_DIM, UI_DIM);
    lv_obj_add_flag(tapz, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(tapz, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(tapz, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_remove_flag(tapz, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(tapz);
    lv_obj_add_event_cb(tapz, commander_cb, LV_EVENT_SHORT_CLICKED, NULL);

    /* Vertical stack: caption / big number / hint. */
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Seat number, then the turn, then the life total. */
    s_seatlbl = lv_label_create(scr);
    lv_label_set_text(s_seatlbl, "");
    lv_obj_set_style_text_font(s_seatlbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_seatlbl, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_add_flag(s_seatlbl, LV_OBJ_FLAG_HIDDEN);

    s_turnlbl = lv_label_create(scr);
    lv_label_set_text(s_turnlbl, "TURN 1");
    lv_obj_set_style_text_font(s_turnlbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_turnlbl, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_set_style_pad_bottom(s_turnlbl, 2, 0);
    lv_obj_set_ext_click_area(s_turnlbl, 14);   /* comfortable target for a finger */
    lv_obj_add_event_cb(s_turnlbl, roll_first_cb, LV_EVENT_CLICKED, NULL);

    s_number = lv_label_create(scr);
    lv_obj_set_style_text_font(s_number, &lv_font_montserrat_48, 0);

    /* Battery indicator floating at the top, inside the ring. */
    s_batt = lv_label_create(scr);
    lv_obj_add_flag(s_batt, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(UI_COL_MUTED), 0);
    lv_label_set_text(s_batt, LV_SYMBOL_BATTERY_FULL " --%");
    /* High enough that the linked-dial readout directly above us clears it. */
    lv_obj_align(s_batt, LV_ALIGN_TOP_MID, 0, 12);

    /* Poison, only when you have any — floats under the battery. */
    s_poison = lv_label_create(scr);
    lv_label_set_text(s_poison, "");
    lv_obj_add_flag(s_poison, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_poison, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_poison, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(s_poison, &lv_font_montserrat_16, 0);
    lv_obj_align(s_poison, LV_ALIGN_TOP_MID, 0, 36);

    /* Bow-out button: a little skull, built from primitives since the stock
     * fonts have no such glyph. Sits low-left, clear of the Pass pill. */
    s_skull = lv_button_create(scr);
    lv_obj_add_flag(s_skull, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_skull, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_skull, 58, 58);
    lv_obj_set_style_radius(s_skull, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_skull, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_width(s_skull, 2, 0);
    lv_obj_set_style_border_color(s_skull, lv_color_hex(UI_COL_DANGER), 0);
    lv_obj_set_style_shadow_width(s_skull, 0, 0);
    lv_obj_set_style_pad_all(s_skull, 0, 0);
    lv_obj_align(s_skull, LV_ALIGN_CENTER, -104, 96);
    lv_obj_add_event_cb(s_skull, skull_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *cranium = lv_obj_create(s_skull);
        lv_obj_remove_style_all(cranium);
        lv_obj_set_size(cranium, 30, 26);
        lv_obj_set_style_radius(cranium, 13, 0);
        lv_obj_set_style_bg_color(cranium, lv_color_hex(UI_COL_TEXT), 0);
        lv_obj_set_style_bg_opa(cranium, LV_OPA_COVER, 0);
        lv_obj_align(cranium, LV_ALIGN_CENTER, 0, -5);

        lv_obj_t *jaw = lv_obj_create(s_skull);
        lv_obj_remove_style_all(jaw);
        lv_obj_set_size(jaw, 16, 9);
        lv_obj_set_style_radius(jaw, 3, 0);
        lv_obj_set_style_bg_color(jaw, lv_color_hex(UI_COL_TEXT), 0);
        lv_obj_set_style_bg_opa(jaw, LV_OPA_COVER, 0);
        lv_obj_align(jaw, LV_ALIGN_CENTER, 0, 11);

        for (int e = 0; e < 2; e++) {
            lv_obj_t *eye = lv_obj_create(s_skull);
            lv_obj_remove_style_all(eye);
            lv_obj_set_size(eye, 8, 9);
            lv_obj_set_style_radius(eye, 4, 0);
            lv_obj_set_style_bg_color(eye, lv_color_hex(UI_COL_BG), 0);
            lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
            lv_obj_align(eye, LV_ALIGN_CENTER, e ? 7 : -7, -6);
        }
    }

    /* Ring of linked dials (Remote mode): one small readout per other dial,
     * positioned around the rim by screen_life_refresh_peers(). Decorative, so
     * they never intercept taps. */
    for (int i = 0; i < NET_MAX_OTHERS; i++) {
        lv_obj_t *a = lv_arc_create(scr);
        lv_obj_set_size(a, 68, 68);
        lv_obj_add_flag(a, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        lv_arc_set_bg_angles(a, 135, 45);          /* 270 deg, opening downward */
        lv_arc_set_range(a, 0, LIFE_BAR_FULL);
        lv_obj_set_style_arc_width(a, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(a, lv_color_hex(UI_COL_TILE), LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
        s_peer[i] = a;

        lv_obj_t *nm = lv_label_create(a);
        lv_label_set_text(nm, "");
        lv_obj_set_style_text_font(nm, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(nm, lv_color_hex(UI_COL_MUTED), 0);
        lv_obj_align(nm, LV_ALIGN_CENTER, 0, -11);
        s_peer_name[i] = nm;

        lv_obj_t *lf = lv_label_create(a);
        lv_label_set_text(lf, "");
        lv_obj_set_style_text_font(lf, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lf, lv_color_hex(UI_COL_TEXT), 0);
        lv_obj_align(lf, LV_ALIGN_CENTER, 0, 8);
        s_peer_life[i] = lf;
    }

    /* Life changes with the dial only (no tap zones), so an accidental tap while
     * swiping between screens can never change the total. */

    /* Hold (not tap) the centre to open Tools — LONG_PRESSED so a stray tap on
     * the number doesn't pop the menu. The long-press time (~1s) is set on the
     * touch indev in app_main.c. */
    lv_obj_t *menu = lv_obj_create(scr);
    lv_obj_remove_style_all(menu);
    lv_obj_set_size(menu, 132, 132);
    lv_obj_set_style_radius(menu, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(menu);
    lv_obj_add_event_cb(menu, menu_cb, LV_EVENT_LONG_PRESSED, NULL);
    /* Short tap on the centre opens Commander too (so the whole face is one tap
     * target); the hold above still opens Tools. */
    lv_obj_add_event_cb(menu, commander_cb, LV_EVENT_SHORT_CLICKED, NULL);

    /* Pass pill sitting in the ring's bottom gap: hands the turn to the next
     * seat (locally, just advances the turn). Floats above the lower zone so it
     * catches its own taps. */
    lv_obj_t *tbtn = lv_button_create(scr);
    lv_obj_add_flag(tbtn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(tbtn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(tbtn, LV_SIZE_CONTENT, 38);
    lv_obj_set_style_pad_hor(tbtn, 16, 0);
    lv_obj_set_style_radius(tbtn, 19, 0);
    lv_obj_set_style_bg_color(tbtn, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_width(tbtn, 1, 0);
    lv_obj_set_style_border_color(tbtn, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_set_style_shadow_width(tbtn, 0, 0);
    lv_obj_align(tbtn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(tbtn, pass_cb, LV_EVENT_CLICKED, NULL);

    s_passbtn_lbl = lv_label_create(tbtn);
    lv_label_set_text(s_passbtn_lbl, "Pass  " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(s_passbtn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_passbtn_lbl, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(s_passbtn_lbl);
    refresh_turn();

    lv_obj_add_event_cb(scr, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    /* The centre hold-for-Tools catcher is created after the turn readout and
     * would otherwise swallow taps on it, so lift the readout above it. */
    lv_obj_move_foreground(s_turnlbl);

    refresh();
    return scr;
}

void screen_life_handle_input(input_event_t ev)
{
    switch (ev) {
    case INPUT_EV_INCREMENT:
        counter_increment(&s_life);
        break;
    case INPUT_EV_DECREMENT:
        counter_decrement(&s_life);
        break;
    default:
        return; /* other events are not meaningful on this screen */
    }

    refresh(); /* dial changes the number silently (no flash) */
}
