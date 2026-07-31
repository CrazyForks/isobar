/* palette.h - color mapping for display and export (Form5, docs/01
 * sec. 4 "Color mapping").
 *
 * The raw image buffer is always grayscale; colorization happens at
 * presentation/export time through the current mode + invert, exactly
 * like the original. Invert is applied to the gray value FIRST
 * (v = 255 - v), then the palette - so in color modes it reverses the
 * ramp, it is not a post-color negative.
 *
 * Modes (anchor colors from the spec):
 *   0 monotone:        gray (R=G=B=v)
 *   1 (no UI name):    blue -> cyan -> green -> yellow -> red
 *                      (not selectable in Form5; only reachable by
 *                      loading a .syn saved with mode 1)
 *   2 "blue ray":      black -> blue -> cyan -> white
 *   3 "color temp":    black -> blue -> cyan -> green -> yellow ->
 *                      red -> white (v=255 forced white)
 */
#ifndef ISOBAR_PALETTE_H
#define ISOBAR_PALETTE_H

#include <cstdint>

/* Build the 256-entry RGB lookup for mode (0..3) and invert (0/1).
 * Unknown modes fall back to monotone (like the original's UI, which
 * shows any non-2/3 mode as "monotone"). */
void palette_build(int mode, int invert, uint8_t out[256][3]);

#endif
