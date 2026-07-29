#include "screen_calibrate.h"

#include "ui_common.h"
#include "nav.h"
#include "petal_hal.h"

#include "lvgl.h"
#include <math.h>

/* Targets in true display pixels: N/E/S/W inset from the round glass, then the
 * centre last. Spread across both axes so the linear fit is well conditioned,
 * and derived from the panel size so they stay inside the glass on any face. */
#define NPT 5
#define CX  (PETAL_DISP_W / 2)
#define CY  (PETAL_DISP_H / 2)
#define INS (PETAL_DISP_W * 4 / 15)   /* 96 px on a 360 px face */
static const lv_point_t TARGETS[NPT] = {
    {CX, CY - INS}, {CX + INS, CY}, {CX, CY + INS}, {CX - INS, CY}, {CX, CY},
};

#define RING_R 18

static lv_obj_t *s_root;
static lv_obj_t *s_target;    /* crosshair ring, absolutely positioned */
static lv_obj_t *s_progress;  /* "1 / 5" */
static lv_obj_t *s_result;    /* centred "Saved" / "Try again", hidden mid-run */
static float     s_raw[NPT][2];
static int       s_idx;
static bool      s_done;

static void place_target(int i)
{
    lv_obj_set_pos(s_target, TARGETS[i].x - RING_R, TARGETS[i].y - RING_R);
}

static void show_step(void)
{
    lv_label_set_text_fmt(s_progress, "%d / %d", s_idx + 1, NPT);
    place_target(s_idx);
}

/* Least-squares fit of disp = scale*raw + offset. */
static void fit_axis(const float *raw, const float *disp, int n,
                     float *scale, float *offset)
{
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
        sx  += raw[i];
        sy  += disp[i];
        sxx += raw[i] * raw[i];
        sxy += raw[i] * disp[i];
    }
    float den = (float)n * sxx - sx * sx;
    if (fabsf(den) < 1e-3f) { *scale = 1.0f; *offset = 0.0f; return; }
    *scale  = ((float)n * sxy - sx * sy) / den;
    *offset = (sy - *scale * sx) / (float)n;
}

/* Popping the overlay from inside the tap's event dispatch is the LVGL
 * re-entrancy that froze the device elsewhere; a one-shot timer fires safely
 * outside dispatch and also lets the result linger for a beat. */
static void pop_timer_cb(lv_timer_t *t) { LV_UNUSED(t); nav_pop(); }

static void finish(void)
{
    float rx[NPT], ry[NPT], dx[NPT], dy[NPT];
    for (int i = 0; i < NPT; i++) {
        rx[i] = s_raw[i][0];  ry[i] = s_raw[i][1];
        dx[i] = (float)TARGETS[i].x;  dy[i] = (float)TARGETS[i].y;
    }
    float coef[PETAL_TOUCH_CALIB_N];
    fit_axis(rx, dx, NPT, &coef[0], &coef[1]);
    fit_axis(ry, dy, NPT, &coef[2], &coef[3]);

    /* A sane panel maps roughly 1:1. Reject a wild fit (a stray/mis-tap) rather
     * than saving a map that would make the screen unusable. */
    bool ok = coef[0] > 0.5f && coef[0] < 2.0f &&
              coef[2] > 0.5f && coef[2] < 2.0f;
    if (ok) ok = petal_touch_calib_set(coef);

    s_done = true;
    lv_obj_add_flag(s_target, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_progress, "");
    if (ok) {
        lv_label_set_text(s_result, LV_SYMBOL_OK "  Saved");
        lv_obj_set_style_text_color(s_result, lv_color_hex(UI_COL_GOLD), 0);
    } else {
        lv_label_set_text(s_result, LV_SYMBOL_WARNING "  Try again");
        lv_obj_set_style_text_color(s_result, lv_color_hex(UI_COL_DANGER), 0);
    }

    lv_timer_t *t = lv_timer_create(pop_timer_cb, 900, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* Full-face tap zone fires this on each release. A swipe travels too far to
 * register as a click, so a right-swipe still pops (cancels) cleanly without
 * recording a stray sample. */
static void tap_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_done || s_idx >= NPT) return;

    uint16_t rx, ry;
    petal_touch_last_raw(&rx, &ry);
    s_raw[s_idx][0] = (float)rx;
    s_raw[s_idx][1] = (float)ry;
    s_idx++;

    if (s_idx >= NPT) finish();
    else show_step();
}

void screen_calibrate_init(void)
{
    s_root = ui_make_round_screen();

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "CALIBRATE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *sub = lv_label_create(s_root);
    lv_label_set_text(sub, "Tap each target");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 50);

    s_progress = lv_label_create(s_root);
    lv_label_set_text(s_progress, "");
    lv_obj_set_style_text_font(s_progress, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_progress, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_align(s_progress, LV_ALIGN_BOTTOM_MID, 0, -24);

    s_result = lv_label_create(s_root);
    lv_label_set_text(s_result, "");
    lv_obj_set_style_text_font(s_result, &lv_font_montserrat_20, 0);
    lv_obj_align(s_result, LV_ALIGN_CENTER, 0, 0);

    /* Crosshair ring + centre dot, absolutely positioned each step. */
    s_target = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_target);
    lv_obj_set_size(s_target, RING_R * 2, RING_R * 2);
    lv_obj_set_style_radius(s_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_target, 3, 0);
    lv_obj_set_style_border_color(s_target, lv_color_hex(UI_COL_GOLD), 0);
    lv_obj_set_style_bg_opa(s_target, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_target, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(s_target);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COL_GOLD), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_center(dot);

    /* Full-face tap zone, created last so it sits above everything. */
    lv_obj_t *zone = ui_make_tap_zone(s_root, tap_cb, NULL);
    lv_obj_set_size(zone, UI_DIM, UI_DIM);
    lv_obj_center(zone);
}

void screen_calibrate_open(void)
{
    s_idx  = 0;
    s_done = false;
    lv_label_set_text(s_result, "");
    lv_obj_remove_flag(s_target, LV_OBJ_FLAG_HIDDEN);
    show_step();
    nav_push(s_root, NULL);
}
