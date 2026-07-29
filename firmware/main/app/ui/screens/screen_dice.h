/*
 * screen_dice.h — a dial-native randomizer overlay (coin / d6 / d20).
 *
 * Reached from the Tools pie. Tap a chip to pick the die; spin the dial (or tap
 * the centre) to roll — the face tumbles briefly and settles on a random result
 * from the ESP32 hardware RNG. Swipe right to go back.
 */
#ifndef LOTUS_SCREEN_DICE_H
#define LOTUS_SCREEN_DICE_H

/** Build the dice overlay once. */
void screen_dice_init(void);

/** Show the dice overlay over the current screen. */
void screen_dice_open(void);

/** Open the roller with the first-player die already selected, so the dial can
 *  be spun straight away. Used by the "ROLL FOR 1ST" prompt on the Life screen. */
void screen_dice_open_first(void);

#endif /* LOTUS_SCREEN_DICE_H */
