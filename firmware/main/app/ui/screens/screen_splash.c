#include "screen_splash.h"

#include "ui_common.h"
#include "nav.h"
#include "lotus.h"

#define SPLASH_MS 2000

/* Sized to the space above the name rather than to the whole face: a 225px
 * centre petal puts the base around y=260 and the tip around y=35, leaving the
 * name clear underneath. The version is not repeated here — it lives on About. */
#define LOTUS_H 225

static lv_obj_t     *s_root;
static lv_timer_t   *s_timer;
static splash_done_fn s_done;

static void timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    s_timer = NULL;
    nav_pop();
    if (s_done) s_done();
}

void screen_splash_init(void)
{
    s_root = ui_make_round_screen();

    lv_obj_t *mark = ui_lotus_create(s_root, LOTUS_H);
    if (mark) lv_obj_align(mark, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *name = lv_label_create(s_root);
    lv_label_set_text(name, UI_APP_NAME);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), 0);
    /* No fill behind the glyphs — otherwise the label's box masks the petals
     * where the two meet. */
    lv_obj_set_style_bg_opa(name, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(name, 0, 0);
    lv_obj_align(name, LV_ALIGN_CENTER, 0, 118);
}

void screen_splash_open(splash_done_fn done)
{
    s_done = done;
    nav_push(s_root, NULL);
    if (s_timer) lv_timer_delete(s_timer);
    s_timer = lv_timer_create(timer_cb, SPLASH_MS, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}
