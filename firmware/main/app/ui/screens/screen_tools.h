/*
 * screen_tools.h — the "hold the middle of the face" Tools pie.
 *
 * A radial pop-up menu (not part of the swipe order) with three wedges:
 *   - Undo     : step the turn counter back one
 *   - Settings : open the scrollable Settings overlay (brightness / about)
 *   - Reset    : new game (asks to confirm first)
 * Spin the dial to move the highlight; tap the centre to open the highlighted
 * wedge, or tap a wedge directly.
 */
#ifndef LOTUS_SCREEN_TOOLS_H
#define LOTUS_SCREEN_TOOLS_H

/** Build the Tools pie once (call after the other screens exist). */
void screen_tools_init(void);

/** Show the Tools pie as an overlay over the current screen. */
void screen_tools_open(void);

#endif /* LOTUS_SCREEN_TOOLS_H */
