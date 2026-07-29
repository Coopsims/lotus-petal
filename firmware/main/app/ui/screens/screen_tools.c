#include "screen_tools.h"

#include "ui_common.h"
#include "nav.h"
#include "game.h"
#include "screen_settings.h"
#include "screen_mode.h"
#include "screen_dice.h"

#include "lvgl.h"
#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The Tools "pie": three wedges around a central readout. Spin the dial to move
 * the highlight, tap the centre to open the highlighted wedge, or tap a wedge
 * directly. Undo and Reset act; Settings opens its own overlay.
 *
 * Wedges are thick LVGL arcs (a donut sector each). Adv-hittest makes only the
 * arc pixels clickable, so taps land on the right wedge. */

enum { W_SETTINGS = 0, W_RESET, W_UNDO, W_DICE, W_COUNT };

typedef struct {
    const char *name;
    const char *icon;
    uint32_t    color;
    int         a_start;   /* LVGL degrees: 0=east, 90=south, clockwise */
    int         a_end;
    int         a_mid;
} wedge_t;

static const wedge_t WEDGES[W_COUNT] = {
    /* Four 90-degree wedges, clockwise from the top (index order = dial order):
     * Settings top, Reset right, Undo bottom, Dice left. */
    [W_SETTINGS] = { "Settings", LV_SYMBOL_SETTINGS, UI_COL_GOLD,   225, 315, 270 },
    [W_RESET]    = { "Reset",    LV_SYMBOL_TRASH,    UI_COL_DANGER, 315,  45,   0 },
    [W_UNDO]     = { "Undo",     LV_SYMBOL_LEFT,     UI_COL_ACCENT,  45, 135,  90 },
    [W_DICE]     = { "Dice",     LV_SYMBOL_SHUFFLE,  0x22D3EE,      135, 225, 180 },
};

#define ARC_SIZE   330
#define ARC_WIDTH  120
#define LABEL_R    108   /* radius for a wedge's own label */

static lv_obj_t *s_root;
static lv_obj_t *s_arc[W_COUNT];
static lv_obj_t *s_center_icon;
static lv_obj_t *s_center_name;
static lv_obj_t *s_confirm;      /* reset confirmation panel (hidden) */
static int       s_sel;

static void update_highlight(void)
{
    for (int i = 0; i < W_COUNT; i++) {
        lv_obj_set_style_arc_opa(s_arc[i], i == s_sel ? LV_OPA_COVER : LV_OPA_30,
                                 LV_PART_MAIN);
    }
    lv_label_set_text(s_center_icon, WEDGES[s_sel].icon);
    lv_label_set_text(s_center_name, WEDGES[s_sel].name);
    lv_obj_set_style_text_color(s_center_icon, lv_color_hex(WEDGES[s_sel].color), 0);
}

static void activate(int which)
{
    switch (which) {
    case W_UNDO:
        game_undo_turn();
        nav_pop();                 /* back to the game */
        break;
    case W_SETTINGS:
        screen_settings_open();
        break;
    case W_RESET:
        lv_obj_remove_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);   /* ask first */
        break;
    case W_DICE:
        screen_dice_open();
        break;
    }
}

static void wedge_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_sel = idx;
    update_highlight();
    activate(idx);
}

static void center_cb(lv_event_t *e)
{
    (void)e;
    activate(s_sel);
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    nav_pop();
}

static void confirm_yes_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    game_reset();
    nav_pop_all();                 /* fresh game, back to the base screen */
    screen_mode_open();            /* ...then pick Local or Remote again */
}

static void confirm_no_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
}

static void pie_enc(int delta)
{
    if (!lv_obj_has_flag(s_confirm, LV_OBJ_FLAG_HIDDEN)) return;  /* confirm open */
    s_sel = ((s_sel + delta) % W_COUNT + W_COUNT) % W_COUNT;
    update_highlight();
}

static lv_obj_t *make_wedge(int idx)
{
    lv_obj_t *arc = lv_arc_create(s_root);
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, WEDGES[idx].a_start, WEDGES[idx].a_end);
    lv_arc_set_rotation(arc, 0);
    /* Visual only: colour the background arc, hide indicator + knob. */
    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(WEDGES[idx].color), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    /* Interactive: hit only the arc pixels, fire CLICKED to activate. */
    lv_obj_add_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(arc, wedge_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    /* Wedge label at the sector's mid-angle. */
    float rad = (float)WEDGES[idx].a_mid * (float)M_PI / 180.0f;
    int ox = (int)(LABEL_R * cosf(rad));
    int oy = (int)(LABEL_R * sinf(rad));
    lv_obj_t *lbl = lv_label_create(s_root);
    lv_label_set_text_fmt(lbl, "%s\n%s", WEDGES[idx].icon, WEDGES[idx].name);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(lbl, LV_ALIGN_CENTER, ox, oy);
    return arc;
}

static void build_confirm(void)
{
    /* Full-screen modal backdrop: catches any tap that misses the buttons so a
     * stray press can't reach a wedge behind the dialog. */
    s_confirm = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_confirm);
    lv_obj_set_size(s_confirm, lv_pct(100), lv_pct(100));
    lv_obj_center(s_confirm);
    lv_obj_set_style_bg_color(s_confirm, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(s_confirm, LV_OPA_70, 0);
    lv_obj_remove_flag(s_confirm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_CLICKABLE);   /* absorb backdrop taps */
    lv_obj_add_event_cb(s_confirm, confirm_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(s_confirm);
    lv_obj_set_size(panel, 220, 160);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_COL_DANGER), 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *q = lv_label_create(panel);
    lv_label_set_text(q, "New Game?");
    lv_obj_set_style_text_font(q, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(q, lv_color_hex(UI_COL_TEXT), 0);

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 190, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *no = lv_button_create(row);
    lv_obj_set_size(no, 80, 48);
    lv_obj_set_style_radius(no, 24, 0);
    lv_obj_set_style_bg_color(no, lv_color_hex(UI_COL_BG), 0);
    lv_obj_add_event_cb(no, confirm_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = lv_label_create(no);
    lv_label_set_text(nl, "Cancel");
    lv_obj_center(nl);

    lv_obj_t *yes = lv_button_create(row);
    lv_obj_set_size(yes, 80, 48);
    lv_obj_set_style_radius(yes, 24, 0);
    lv_obj_set_style_bg_color(yes, lv_color_hex(UI_COL_DANGER), 0);
    lv_obj_add_event_cb(yes, confirm_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *yl = lv_label_create(yes);
    lv_label_set_text(yl, "Reset");
    lv_obj_center(yl);
}

void screen_tools_init(void)
{
    s_root = ui_make_round_screen();

    for (int i = 0; i < W_COUNT; i++) {
        s_arc[i] = make_wedge(i);
    }

    /* Central readout / tap-to-open disk. */
    lv_obj_t *hub = lv_obj_create(s_root);
    lv_obj_set_size(hub, 128, 128);
    lv_obj_center(hub);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);   /* mask the wedge centres */
    lv_obj_set_style_border_width(hub, 0, 0);
    lv_obj_remove_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hub, center_cb, LV_EVENT_CLICKED, NULL);

    s_center_icon = lv_label_create(hub);
    lv_obj_set_style_text_font(s_center_icon, &lv_font_montserrat_36, 0);
    lv_obj_align(s_center_icon, LV_ALIGN_CENTER, 0, -14);

    s_center_name = lv_label_create(hub);
    lv_obj_set_style_text_font(s_center_name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_center_name, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_align(s_center_name, LV_ALIGN_CENTER, 0, 22);

    /* Close (✕) at the bottom edge, inside the glass. */
    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 44, 44);
    lv_obj_set_style_radius(close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_add_event_cb(close, close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(cl, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_center(cl);

    build_confirm();
    lv_obj_move_foreground(s_confirm);   /* keep the dialog above the wedges */

    s_sel = W_SETTINGS;
    update_highlight();
}

void screen_tools_open(void)
{
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    s_sel = W_SETTINGS;
    update_highlight();
    nav_push(s_root, pie_enc);
}
