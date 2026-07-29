#include "nav.h"

#define NAV_MAX 6

typedef struct {
    lv_obj_t   *scr;   /* the overlay screen shown at this level */
    lv_obj_t   *prev;  /* screen to reload when this level is popped */
    nav_enc_fn  enc;   /* dial handler while this level is on top */
} nav_frame_t;

static nav_frame_t s_stk[NAV_MAX];
static int s_sp;

void nav_push(lv_obj_t *scr, nav_enc_fn enc)
{
    if (!scr || s_sp >= NAV_MAX) return;
    s_stk[s_sp].scr  = scr;
    s_stk[s_sp].prev = lv_screen_active();
    s_stk[s_sp].enc  = enc;
    s_sp++;
    lv_screen_load(scr);
}

void nav_pop(void)
{
    if (s_sp <= 0) return;
    s_sp--;
    lv_screen_load(s_stk[s_sp].prev);
}

void nav_pop_all(void)
{
    if (s_sp <= 0) return;
    lv_obj_t *base = s_stk[0].prev;   /* the game screen we first came from */
    s_sp = 0;
    lv_screen_load(base);
}

bool nav_active(void)
{
    return s_sp > 0;
}

void nav_encoder(int delta)
{
    if (s_sp > 0 && s_stk[s_sp - 1].enc) {
        s_stk[s_sp - 1].enc(delta);
    }
}
