#include "ui_common.h"

static void flash_opa_cb(void *obj, int32_t opa)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)opa, 0);
}

void ui_flash(lv_obj_t *overlay)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, overlay);
    lv_anim_set_exec_cb(&a, flash_opa_cb);
    lv_anim_set_values(&a, 110, 0);          /* peak highlight -> transparent */
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

lv_obj_t *ui_make_flash_overlay(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *ov = lv_obj_create(parent);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, lv_pct(100), lv_pct(100));
    lv_obj_center(ov);
    lv_obj_set_style_bg_color(ov, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(ov, LV_RADIUS_CIRCLE, 0);
    /* Purely decorative: float above any layout, never intercept input. */
    lv_obj_add_flag(ov, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    return ov;
}

lv_obj_t *ui_make_round_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* The physical glass is round; the square framebuffer's corners are off the
     * visible glass, so we simply flood-fill the whole face with the dark
     * background (no circle clip needed). Screens keep content within the
     * inscribed circle so nothing important lands near the hidden corners. */
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    return scr;
}

lv_obj_t *ui_make_tap_zone(lv_obj_t *parent, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *z = lv_obj_create(parent);
    lv_obj_remove_style_all(z);
    lv_obj_set_style_bg_opa(z, LV_OPA_TRANSP, 0);
    /* Float above the parent's layout; caller positions it with size + align. */
    lv_obj_add_flag(z, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(z, LV_OBJ_FLAG_SCROLLABLE);
    /* Let swipe gestures reach the screen root for navigation; taps are still
     * handled here (a swipe travels too far to register as a click). */
    lv_obj_add_flag(z, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(z, cb, LV_EVENT_CLICKED, user_data);
    return z;
}

lv_obj_t *ui_make_back_button(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(b, 46, 46);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_TILE), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_MUTED), 0);
    lv_obj_center(l);
    return b;
}
