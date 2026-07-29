/*
 * lotus.h — the Lotus Petal mark, drawn rather than shipped as a bitmap.
 *
 * Five petals fanning up from a common base, each a closed polyline whose
 * half-width follows sin(pi*t) along its length so it comes to a point. Costs a
 * few hundred bytes of geometry instead of a decoded image, and stays crisp at
 * any size — which is the point, since it is wanted both large on the splash and
 * thumbnail-sized on the About page.
 *
 * Stroke is derived from the size rather than passed in: the petals converge to
 * a single point at the base, so a stroke that does not shrink with the mark
 * turns that junction into a blob.
 */
#ifndef LOTUS_LOTUS_H
#define LOTUS_LOTUS_H

#include "lvgl.h"

/**
 * ui_lotus_create — draw a lotus into `parent`.
 * @param height  length of the tallest (centre) petal, in pixels
 * @return the container holding it, sized to the artwork; position it with
 *         lv_obj_align(). LVGL keeps our point arrays, so instances come from a
 *         small fixed pool — plenty, as screens are built once at boot.
 */
lv_obj_t *ui_lotus_create(lv_obj_t *parent, int height);

#endif /* LOTUS_LOTUS_H */
