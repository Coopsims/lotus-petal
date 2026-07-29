#include "screen_dice.h"

#include "ui_common.h"
#include "nav.h"
#include "game.h"

#include "petal_hal.h"
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Laid out for a round face: the die types ring the bezel and the result sits in
 * the middle, rather than stacking rows that the glass would clip at the corners.
 * Ten evenly spaced slots — nine dice plus a Back chip at the bottom.
 */
enum { DIE_COIN = 0, DIE_D4, DIE_D6, DIE_D8, DIE_D10, DIE_D12, DIE_D20,
       DIE_D100, DIE_CUSTOM, DIE_N };

static const int         SIDES[DIE_N] = { 2, 4, 6, 8, 10, 12, 20, 100, 0 };
static const char *const NAMES[DIE_N] = { "Coin", "d4", "d6", "d8", "d10",
                                          "d12", "d20", "d100", "dN" };

#define SLOTS      10
#define BACK_SLOT  5            /* bottom of the ring */
#define RING_R     136
#define CHIP_W     56
#define CHIP_H     40

/* "Keep going": refreshed on every detent, so it never runs out mid-spin. */
#define SPIN_TICKS 1000

/* Half a second from the last detent to the result, and that is the whole of it:
 * QUIET_MS deciding the knob has actually stopped, then TAIL_MS still turning.
 * The tail was previously measured on its own, so the wait was really the two
 * added together.
 *
 * Still unriggable — every tick draws a fresh face, so what lands has nothing to
 * do with what was showing when you let go. */
#define ROLL_TICK_MS 25
#define QUIET_MS     100
#define TAIL_MS      100
#define TAIL_TICKS   (TAIL_MS / ROLL_TICK_MS)

#define CUSTOM_MIN 2
#define CUSTOM_MAX 999
static int s_custom_sides = 3;

static int die_sides(int die)
{
    return (die == DIE_CUSTOM) ? s_custom_sides : SIDES[die];
}

/* True while this roll is the opening d20 roll-off, so the result is reported to
 * the table instead of just being shown. */
static bool s_for_first;

static lv_obj_t  *s_root;
static lv_obj_t  *s_chip[DIE_N];
static lv_obj_t  *s_result;
static lv_obj_t  *s_caption;   /* die name / custom side count, under the value */
static lv_obj_t  *s_flash;
static lv_timer_t *s_roll_timer;
static lv_timer_t *s_settle_timer;

static int s_die = DIE_D20;
static int s_value = 1;
static int s_roll_ticks;

/* Dice sit in every slot except the one reserved for Back. */
static int slot_of(int die)
{
    return (die < BACK_SLOT) ? die : die + 1;
}

static void place_at_slot(lv_obj_t *o, int slot)
{
    double ang = (-90.0 + (double)slot * 360.0 / SLOTS) * M_PI / 180.0;
    lv_obj_align(o, LV_ALIGN_CENTER, (int)lround(RING_R * cos(ang)),
                                     (int)lround(RING_R * sin(ang)));
}

static void show_value(void)
{
    if (s_die == DIE_COIN) {
        lv_obj_set_style_text_font(s_result, &lv_font_montserrat_28, 0);
        lv_label_set_text(s_result, s_value == 1 ? "Heads" : "Tails");
    } else {
        lv_obj_set_style_text_font(s_result, &lv_font_montserrat_48, 0);
        lv_label_set_text_fmt(s_result, "%d", s_value);
    }

    if (s_die == DIE_CUSTOM) {
        lv_label_set_text_fmt(s_caption, "dial sets sides: %d", s_custom_sides);
    } else if (s_for_first) {
        lv_label_set_text(s_caption, "roll for first");
    } else {
        lv_label_set_text(s_caption, NAMES[s_die]);
    }
}

static void update_chips(void)
{
    for (int i = 0; i < DIE_N; i++) {
        bool sel = (i == s_die);
        lv_obj_set_style_bg_color(s_chip[i],
            lv_color_hex(sel ? UI_COL_ACCENT : UI_COL_TILE), 0);
        lv_obj_set_style_border_width(s_chip[i], sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_chip[i], lv_color_hex(UI_COL_TEXT), 0);
    }
}

static void roll_face(void)
{
    s_value = 1 + (int)(petal_random() % (uint32_t)die_sides(s_die));
}

/* Whatever started the roll, this ends it. */
static void settle(void)
{
    ui_flash(s_flash);
    /* A roll-off result belongs to the table, so report it. */
    if (s_for_first) game_roll_for_first(s_value);
}

/* Tap-to-roll: flick through a few faces on a timer, then settle. */
static void roll_tick(lv_timer_t *t)
{
    if (s_roll_ticks <= 0) {
        lv_timer_pause(t);
        settle();
        return;
    }
    roll_face();
    s_roll_ticks--;
    show_value();
}

/* Spinning the dial: the face changes with the knob, and settles once it has
 * been still for a moment — so the number follows your hand instead of playing
 * a fixed animation per detent. */
/* The knob has gone quiet: stop refreshing the countdown and let it run out.
 * The faces are already tumbling, so this only decides when they stop — it does
 * NOT take whatever face is showing, which would let you time your release. */
static void settle_cb(lv_timer_t *t)
{
    lv_timer_pause(t);
    s_roll_ticks = 14 + (int)(petal_random() % 22);   /* ~0.6-1.4s more */
}

static bool rolling(void)
{
    return s_roll_ticks > 0;
}

static void do_roll(void)
{
    if (s_settle_timer) lv_timer_pause(s_settle_timer);
    s_roll_ticks = TAIL_TICKS;
    lv_timer_reset(s_roll_timer);
    lv_timer_resume(s_roll_timer);
}

static void chip_cb(lv_event_t *e)
{
    s_die   = (int)(intptr_t)lv_event_get_user_data(e);
    s_value = 1;
    if (s_value > die_sides(s_die)) s_value = die_sides(s_die);
    update_chips();
    show_value();
}

static void roll_cb(lv_event_t *e)
{
    (void)e;
    do_roll();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    nav_pop();
}

/* The dial rolls — except on the custom die, where it picks the side count and
 * you tap the middle to roll (a knob is the natural way to dial in a number). */
static void dice_enc(int delta)
{
    if (s_die == DIE_CUSTOM && !rolling()) {
        s_custom_sides += delta;
        if (s_custom_sides < CUSTOM_MIN) s_custom_sides = CUSTOM_MIN;
        if (s_custom_sides > CUSTOM_MAX) s_custom_sides = CUSTOM_MAX;
        if (s_value > s_custom_sides) s_value = s_custom_sides;
        show_value();
        return;
    }

    /* Keep the faces turning for as long as the knob does. Topping the counter
     * up on each detent means the animation never pauses — previously it went
     * still while we waited to see whether you had finished, then started up
     * again, which read as a stutter. */
    s_roll_ticks = SPIN_TICKS;
    lv_timer_resume(s_roll_timer);

    /* Restarted on every detent, so it only fires once the spinning stops. */
    lv_timer_reset(s_settle_timer);
    lv_timer_resume(s_settle_timer);
}

void screen_dice_init(void)
{
    s_root = ui_make_round_screen();

    /* Central result disk — tap anywhere on it to roll. */
    lv_obj_t *disk = lv_obj_create(s_root);
    lv_obj_set_size(disk, 156, 156);
    lv_obj_center(disk);
    lv_obj_set_style_radius(disk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disk, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_border_width(disk, 0, 0);
    lv_obj_set_style_pad_all(disk, 0, 0);
    lv_obj_remove_flag(disk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(disk, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(disk, roll_cb, LV_EVENT_CLICKED, NULL);

    s_flash = ui_make_flash_overlay(disk, UI_COL_ACCENT);

    s_result = lv_label_create(disk);
    lv_obj_set_style_text_font(s_result, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_result, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_align(s_result, LV_ALIGN_CENTER, 0, -12);

    s_caption = lv_label_create(disk);
    lv_obj_set_style_text_font(s_caption, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_caption, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_align(s_caption, LV_ALIGN_CENTER, 0, 34);

    /* Die-type chips around the bezel. */
    for (int i = 0; i < DIE_N; i++) {
        lv_obj_t *chip = lv_button_create(s_root);
        lv_obj_set_size(chip, CHIP_W, CHIP_H);
        lv_obj_set_style_radius(chip, CHIP_H / 2, 0);
        lv_obj_set_style_shadow_width(chip, 0, 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_add_event_cb(chip, chip_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        place_at_slot(chip, slot_of(i));
        s_chip[i] = chip;

        lv_obj_t *lbl = lv_label_create(chip);
        lv_label_set_text(lbl, NAMES[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_TEXT), 0);
        lv_obj_center(lbl);
    }

    /* Back sits in the reserved slot at the bottom of the ring. */
    lv_obj_t *back = lv_button_create(s_root);
    lv_obj_set_size(back, CHIP_W, CHIP_H);
    lv_obj_set_style_radius(back, CHIP_H / 2, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    place_at_slot(back, BACK_SLOT);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(bl, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_center(bl);

    s_roll_timer = lv_timer_create(roll_tick, ROLL_TICK_MS, NULL);
    lv_timer_pause(s_roll_timer);

    s_settle_timer = lv_timer_create(settle_cb, QUIET_MS, NULL);
    lv_timer_pause(s_settle_timer);

    update_chips();
    show_value();
}

void screen_dice_open(void)
{
    s_for_first = false;
    update_chips();
    show_value();
    nav_push(s_root, dice_enc);
}

void screen_dice_open_first(void)
{
    s_die       = DIE_D20;
    s_value     = 1;
    s_for_first = true;
    update_chips();
    show_value();
    nav_push(s_root, dice_enc);
}
