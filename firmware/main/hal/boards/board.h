/*
 * board.h — selects the board this build targets.
 *
 * Private to the HAL: nothing in app/ includes this, or any header it pulls in.
 * To add a board, drop a header next to this one that defines the same BOARD_*
 * names and point the include below at it. Keep the app-visible half
 * (../petal_config.h) in step — hal_display.c static-asserts that they agree.
 */
#ifndef PETAL_BOARD_H
#define PETAL_BOARD_H

#include "board_round360_knob.h"

#endif /* PETAL_BOARD_H */
