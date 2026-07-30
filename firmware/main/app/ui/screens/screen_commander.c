#include "screen_commander.h"

#include "counter.h"
#include "ui_common.h"
#include "nav.h"
#include "game.h"
#include "net_link.h"
#include "screens/screen_life.h"

#include "lvgl.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Commander damage, laid out like the table itself: the other players ring the
 * bezel, you sit in the middle. Damage is stored per seat position (1..players)
 * rather than per "opponent index", so your own seat has a slot too — a
 * commander can end up damaging its own controller, and there was previously
 * nowhere to record that.
 *
 * Tap a wedge (or the hub) to select whose commander is hitting you; the dial
 * then adjusts it, mirroring into your life total.
 */
#define MAX_SEATS        8

#define ARC_SIZE   300
#define ARC_WIDTH  86
#define LABEL_R    104
#define WEDGE_GAP  4.0
#define HUB_D      132

static counter_t s_dmg[MAX_SEATS];          /* indexed by position - 1 */
static lv_obj_t *s_arc[MAX_SEATS];          /* ring slots (opponents only) */
static lv_obj_t *s_wname[MAX_SEATS];
static lv_obj_t *s_wval[MAX_SEATS];

static int s_players = 2;      /* seats at the table */
static int s_sel_pos = 1;      /* position whose damage the dial adjusts */

static lv_obj_t *s_root;
static lv_obj_t *s_hub;
static lv_obj_t *s_hub_name;
static lv_obj_t *s_hub_val;
static lv_obj_t *s_hub_flash;

/* Our own seat, 1-based; locally there is no seating, so we are first. */
static int my_pos(void)
{
    int mine = (game_mode() == GAME_MODE_REMOTE) ? net_link_my_order() : 1;
    if (mine < 1 || mine > s_players) mine = 1;
    return mine;
}

static int ring_count(void)
{
    return s_players > 1 ? s_players - 1 : 0;
}

/* Ring slot -> table position, skipping our own seat. */
static int slot_pos(int slot)
{
    int mine = my_pos(), n = 0;
    for (int pos = 1; pos <= s_players; pos++) {
        if (pos == mine) continue;
        if (n == slot) return pos;
        n++;
    }
    return 1;
}

static void paint_value(lv_obj_t *lbl, int pos)
{
    char buf[8];
    counter_value_text(&s_dmg[pos - 1], buf, sizeof(buf));
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl,
        lv_color_hex(counter_at_max(&s_dmg[pos - 1]) ? UI_COL_DANGER : UI_COL_TEXT), 0);
}

static void refresh_all(void)
{
    int mine = my_pos();

    lv_label_set_text(s_hub_name, "You");
    paint_value(s_hub_val, mine);
    /* The hub is a target like any other, so it shows when it is selected. */
    lv_obj_set_style_border_width(s_hub, s_sel_pos == mine ? 3 : 0, 0);
    lv_obj_set_style_border_color(s_hub, lv_color_hex(UI_COL_ACCENT), 0);

    for (int i = 0; i < MAX_SEATS; i++) {
        if (i >= ring_count()) {
            lv_obj_add_flag(s_arc[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_wname[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_wval[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int pos = slot_pos(i);
        bool sel = (pos == s_sel_pos);

        lv_obj_set_style_arc_color(s_arc[i],
            lv_color_hex(sel ? UI_COL_ACCENT : UI_COL_TILE), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(s_arc[i], sel ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
        lv_label_set_text_fmt(s_wname[i], "P%d", pos);
        lv_obj_set_style_text_color(s_wname[i],
            lv_color_hex(sel ? UI_COL_TEXT : UI_COL_MUTED), 0);
        paint_value(s_wval[i], pos);

        lv_obj_remove_flag(s_arc[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_wname[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_wval[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void wedge_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot >= ring_count()) return;
    s_sel_pos = slot_pos(slot);
    refresh_all();
}

static void hub_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_sel_pos = my_pos();
    refresh_all();
}

/* One step of commander damage on the selected seat, mirrored into life:
 * +1 damage => -1 life. Only a real change (not a clamp) moves life. */
static void adjust_selected(int dir)
{
    counter_t *c = &s_dmg[s_sel_pos - 1];
    int before = c->value;
    if (dir > 0) counter_increment(c);
    else         counter_decrement(c);

    int applied = c->value - before;
    if (applied == 0) return;

    screen_life_apply_delta(-applied);
    refresh_all();
    ui_flash(s_hub_flash);
}

static void commander_enc(int delta)
{
    int dir = (delta > 0) ? 1 : -1;
    for (int i = 0, n = abs(delta); i < n; i++) adjust_selected(dir);
}

/* Lay ring slot `i` of `n` out as a donut sector. */
static void place_wedge(int i, int n)
{
    double step = 360.0 / n;
    double gap  = (n > 1) ? WEDGE_GAP : 0.0;
    double base = 270.0 - step / 2.0;
    int s  = ((int)lround(base + i * step + gap / 2.0) % 360 + 360) % 360;
    int en = ((int)lround(base + (i + 1) * step - gap / 2.0) % 360 + 360) % 360;
    if (n == 1) { s = 0; en = 360; }
    lv_arc_set_bg_angles(s_arc[i], s, en);

    double rad = (270.0 + i * step) * M_PI / 180.0;
    int ox = (int)lround(LABEL_R * cos(rad));
    int oy = (int)lround(LABEL_R * sin(rad));
    lv_obj_align(s_wname[i], LV_ALIGN_CENTER, ox, oy - 15);
    lv_obj_align(s_wval[i],  LV_ALIGN_CENTER, ox, oy + 8);
}

static void make_wedge(int i)
{
    lv_obj_t *arc = lv_arc_create(s_root);
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(arc, wedge_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    s_arc[i] = arc;

    s_wname[i] = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_wname[i], &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wname[i], lv_color_hex(UI_COL_MUTED), 0);

    s_wval[i] = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_wval[i], &lv_font_montserrat_24, 0);
}

void screen_commander_init(void)
{
    s_root = ui_make_round_screen();

    for (int i = 0; i < MAX_SEATS; i++) {
        s_dmg[i] = (counter_t){
            .name = "Cmdr", .type = COUNTER_TYPE_INT,
            .value = 0, .min = 0, .max = COMMANDER_LETHAL, .wrap = false,
        };
        make_wedge(i);
    }

    /* You, in the middle — also a damage target, and the inner mask for the
     * wedges. */
    s_hub = lv_obj_create(s_root);
    lv_obj_set_size(s_hub, HUB_D, HUB_D);
    lv_obj_center(s_hub);
    lv_obj_set_style_radius(s_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_hub, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(s_hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_hub, 0, 0);
    lv_obj_set_style_pad_all(s_hub, 0, 0);
    lv_obj_remove_flag(s_hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_hub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_hub, hub_cb, LV_EVENT_CLICKED, NULL);

    s_hub_flash = ui_make_flash_overlay(s_hub, UI_COL_ACCENT);

    s_hub_name = lv_label_create(s_hub);
    lv_obj_set_style_text_font(s_hub_name, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_hub_name, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_align(s_hub_name, LV_ALIGN_CENTER, 0, -30);

    s_hub_val = lv_label_create(s_hub);
    lv_obj_set_style_text_font(s_hub_val, &lv_font_montserrat_48, 0);
    lv_obj_align(s_hub_val, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "COMMANDER DMG");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *hint = lv_label_create(s_root);
    lv_label_set_text(hint, "swipe " LV_SYMBOL_RIGHT " back");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    s_sel_pos = 1;
}

/* `n` is the number of seats at the table, ours included. */
void screen_commander_set_opponents(int n)
{
    if (n < 2) n = 2;
    if (n > MAX_SEATS) n = MAX_SEATS;
    s_players = n;

    for (int i = 0; i < MAX_SEATS; i++) s_dmg[i].value = 0;
    for (int i = 0; i < ring_count(); i++) place_wedge(i, ring_count());

    /* Default the dial to the first opponent, not to ourselves. */
    s_sel_pos = ring_count() > 0 ? slot_pos(0) : my_pos();
    refresh_all();
}

void screen_commander_open(void)
{
    /* Seating may have changed since the grid was built. */
    for (int i = 0; i < ring_count(); i++) place_wedge(i, ring_count());
    if (s_sel_pos < 1 || s_sel_pos > s_players) s_sel_pos = my_pos();
    refresh_all();
    nav_push(s_root, commander_enc);
}

void screen_commander_reset(void)
{
    for (int i = 0; i < MAX_SEATS; i++) s_dmg[i].value = 0;
    s_sel_pos = ring_count() > 0 ? slot_pos(0) : my_pos();
    refresh_all();
}

int screen_commander_max_damage(void)
{
    int worst = 0;
    for (int i = 0; i < s_players && i < MAX_SEATS; i++) {
        if (s_dmg[i].value > worst) worst = s_dmg[i].value;
    }
    return worst;
}

int screen_commander_opponent_count(void)
{
    return s_players;
}

int screen_commander_get_damage(int i)
{
    return (i >= 0 && i < s_players) ? s_dmg[i].value : 0;
}

void screen_commander_set_damage(int i, int v)
{
    if (i < 0 || i >= s_players) return;
    if (v < 0) v = 0;
    if (v > COMMANDER_LETHAL) v = COMMANDER_LETHAL;
    s_dmg[i].value = v;
    refresh_all();
}
