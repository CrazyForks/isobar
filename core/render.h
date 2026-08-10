/* render.h - turning a FaxImage into pixels for export, print and
 * auto-save (docs/01 sec. 4 "Save image" / "Print").
 *
 * These were static helpers inside gui/main.cpp until they grew a test:
 * nothing here touches FLTK or any widget - it is box-averaging, palette
 * lookup and a rotation - so it belongs beside the image it renders, and
 * cli/render-test.cpp can then pin it headlessly. The GUI keeps
 * everything that IS FLTK: the file chooser, the Form6 dialog, the
 * progress window, and the Fl_Image scaling that bakes the print size in.
 *
 * "kind" in the Save-image dialog selects the scale: kinds 1..3 are the
 * 3x3 / 2x2 / 1x1 box-average renders; kind 0 ("Current view") renders at
 * 1x1 and is then scaled to 760 px wide by the caller.
 */
#ifndef ISOBAR_RENDER_H
#define ISOBAR_RENDER_H

#include "syncscan.h"

#include <cstdint>
#include <vector>

/* Box-average `img` down by `scale` and map through the 256-entry palette.
 * `out` receives w*h packed RGB (3 bytes/px, top row first). A scale of 1
 * is the plain raster - which is exactly what the print path wants. */
void render_export_rgb(const FaxImage &img, int scale,
                       const uint8_t pal[256][3],
                       std::vector<uint8_t> &out, int *ow, int *oh);

/* Box-average `img` down by `scale` as GRAYSCALE, 1 byte/px, optionally
 * inverted. The auto-save PNG: never colorized however the display
 * palette is set, because that file is the small archival one (user
 * decision 2026-08-09). */
void render_export_gray(const FaxImage &img, int scale, int invert,
                        std::vector<uint8_t> &out, int *ow, int *oh);

/* Rotate a packed-RGB buffer 90 degrees counter-clockwise, updating the
 * width and height through *w and *h.
 * The "Land." export. The spec only says "transposed"; the direction was
 * a guess until user testing 2026-07-31 showed CW gives an upside-down
 * export of a sideways-transmitted JMH chart and CCW the upright one. */
void render_rotate90ccw(std::vector<uint8_t> &rgb, int *w, int *h);

/* Height on the page, in printer units, for a print of `lines` received
 * lines onto a printable area `page_h` tall.
 *
 * The original stretches its FULL fixed 1500x2280 buffer over the whole
 * page (StretchDIBits(0,0,PageWidth,PageHeight,0,0,1500,2280)), so one
 * line is always page_h/2280 tall no matter how many arrived - the unused
 * tail simply prints black. We skip the black tail, so we must scale by
 * that same 2280-line ruler and leave the rest of the page blank.
 * Stretching `lines` over the whole page instead makes a partial
 * reception 2280/lines too tall: a 1157-line print came out ~2x
 * stretched (user test 2026-08-01), which is the bug this rule exists to
 * prevent and render-test pins. Clamped to the page for a receive buffer
 * that ran past 2280 lines (DEVIATIONS #17). */
int render_print_height(int page_h, int lines);

#endif
