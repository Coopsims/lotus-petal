/*
 * screen_order.h — "pick your seat": once the d20 roll-off has decided who plays
 * first, everyone else claims a turn position from what is still free.
 */
#ifndef LOTUS_SCREEN_ORDER_H
#define LOTUS_SCREEN_ORDER_H

void screen_order_init(void);
void screen_order_open(void);

#endif /* LOTUS_SCREEN_ORDER_H */
