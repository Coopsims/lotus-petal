#include "lotus.h"
#include "ui_common.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PETALS    5
#define SAMPLES   9                    /* points per side */
#define PETAL_PTS (2 * SAMPLES + 1)    /* up one side, down the other, closed */
#define MAX_LOTUS 3                    /* splash + about, with room to spare */

/* Proportions of the mark, all relative to the centre petal's length. Angles are
 * from vertical; the outer pair are splayed but kept under 70 degrees so that at
 * full size their tips still land inside the round glass. */
static const float ANGLE[PETALS] = { -68.0f, -35.0f, 0.0f, 35.0f, 68.0f };
static const float LEN_F[PETALS] = { 0.579f, 0.842f, 1.000f, 0.842f, 0.579f };
static const float HALF_F[PETALS] = { 0.140f, 0.175f, 0.204f, 0.175f, 0.140f };

/* LVGL holds onto the arrays handed to lv_line_set_points(), so they have to
 * outlive this call. */
static lv_point_precise_t s_pts[MAX_LOTUS][PETALS][PETAL_PTS];
static int s_used;

lv_obj_t *ui_lotus_create(lv_obj_t *parent, int height)
{
    if (s_used >= MAX_LOTUS || height < 8) return NULL;
    lv_point_precise_t (*pts)[PETAL_PTS] = s_pts[s_used++];

    /* Proportional to the mark: all five petals meet at one point, and a stroke
     * that stays fixed while the mark shrinks fattens that junction into a
     * solid blob. */
    int stroke = height / 45;
    if (stroke < 2) stroke = 2;
    if (stroke > 7) stroke = 7;

    const float H  = (float)height;
    /* Roomier than the artwork on purpose. LVGL clips children to their parent,
     * and the outer petals are splayed far enough that their widest points fall
     * slightly BELOW the base line — with the box sized tight to the shape those
     * edges were being sliced off, which looks like the flower running into an
     * invisible container. */
    const int   w  = (int)(H * 1.50f);
    const int   h  = (int)(H * 1.20f);
    const float cx = (float)w * 0.5f;
    const float cy = (float)h - H * 0.12f;  /* petals grow up from near the base */

    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, w, h);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    /* Spell out that it paints nothing: the mark is line art, and a container
     * with any fill of its own would sit as a block over whatever it overlaps. */
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);

    for (int p = 0; p < PETALS; p++) {
        float a = ANGLE[p] * (float)M_PI / 180.0f;
        float ca = cosf(a), sa = sinf(a);
        float len = LEN_F[p] * H, half = HALF_F[p] * H;
        int n = 0;

        for (int side = 0; side < 2; side++) {
            for (int i = 0; i < SAMPLES; i++) {
                /* Up the left side, then back down the right. */
                int idx = (side == 0) ? i : (SAMPLES - 1 - i);
                float t = (float)idx / (float)(SAMPLES - 1);
                float wide = half * sinf((float)M_PI * t);
                float x = (side == 0) ? -wide : wide;
                float y = -len * t;          /* screen y grows downward */

                pts[p][n].x = (lv_value_precise_t)(cx + (x * ca - y * sa));
                pts[p][n].y = (lv_value_precise_t)(cy + (x * sa + y * ca));
                n++;
            }
        }
        pts[p][n] = pts[p][0];               /* close the outline */

        lv_obj_t *line = lv_line_create(box);
        lv_line_set_points(line, pts[p], PETAL_PTS);
        lv_obj_set_style_line_color(line, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_line_width(line, stroke, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_set_pos(line, 0, 0);
    }
    return box;
}
