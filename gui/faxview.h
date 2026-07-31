/* faxview.h - fax image preview widget (the original's Image1).
 *
 * ALWAYS shows the column view (docs/01 sec. 4): the 1500-px lines are
 * drawn sideways, line start at the bottom - never the raster as rows.
 * The original keeps this orientation during reception, after stop, and
 * on .syn load alike; rotated viewing is the Vertical button's in-place
 * rotate. Idle clicking zooms (1/3 -> 1/2 -> 1:1, same column
 * orientation) and dragging pans (docs/01 sec. 4 "Zoom/pan").
 * Fixed 760x500 bitmap inside an Fl_Scroll (Form1 geometry, docs/05).
 */
#ifndef ISOBAR_FAXVIEW_H
#define ISOBAR_FAXVIEW_H

#include "../core/syncscan.h"   /* FaxImage */

#include <FL/Fl_Widget.H>

class Fl_Image;

class FaxView : public Fl_Widget {
public:
    /* w is the fixed display width in px (760, like Image1). */
    FaxView(int x, int y, int w, int h);
    ~FaxView();

    /* Replace the shown image; renders through the current zoom level
     * (docs/01 sec. 4 "Zoom/pan"). Zoom 0 = the COLUMN view, exactly
     * like the original's .syn-load redraw: screen col j = lines
     * 3j..3j+2 averaged, screen row k = pixels 3*(499-k)..+2 averaged,
     * palette applied, fixed 760x500 bitmap. Zoom 1 = 1/2 (2x2 box)
     * and zoom 2 = 1:1 keep the same column orientation with viewport
     * offsets. Lines past the end read black, like the original's
     * zeroed buffer. Keeps a private copy for zoom/pan re-renders.
     * An empty image restores the blank 760x500 panel.
     * Call from the UI thread only. */
    void set_image(const FaxImage &img);

    /* Reset the zoom level to 0 (1/3) and the viewport offsets - the
     * original resets dword_4F25C0 on reception start, .syn load,
     * Clear and the auto-save clear (docs/01 sec. 4 "Zoom/pan").
     * State only; the next set_image renders at zoom 0. */
    void reset_zoom();

    /* DEV AID (--test-zoom): zoom in to `level` as if center-clicked
     * at (380,250), using the original's zoom-in formulas. */
    void dev_zoom_to(int level);

    /* Set the 256-entry color lookup (core/palette.h) used to map gray
     * values to RGB. Does NOT re-render by itself - call set_image
     * again for that (like the original, where Button10 re-renders
     * after the Form5 OK). Default is plain grayscale. */
    void set_palette(const uint8_t pal[256][3]);

    /* Live reception preview (docs/01 sec. 4): like the original, the
     * incoming lines are drawn SIDEWAYS - one column per 3 LINES
     * (3-line average), each column 500 px tall (3:1 vertical), so the
     * preview is 1/3 scale in both axes (aspect preserved), with a cyan
     * "Scanning / Line# n" HUD and a cursor line at the frontier.
     * live_begin() clears and enters live mode; live_column() feeds one
     * 1500-px line (plots every 3rd); live_end() leaves live mode and
     * re-renders img through the same column geometry (the original
     * just keeps the bitmap and draws an end marker; re-rendering
     * gives the same picture and includes the <3-line tail).
     * Call from the UI thread only. */
    void live_begin();
    void live_column(const uint8_t *line1500);
    void live_end(const FaxImage &img);

    void draw() override;
    int  handle(int ev) override;

private:
    /* re-render the 760x500 viewport from src_ at the current zoom */
    void render();
    /* click = zoom in/out, drag = pan (docs/01 sec. 4 "Zoom/pan") */
    void zoom_in(int cx, int cy);
    void zoom_out();
    void pan(int dx, int dy);
    /* buffer read: lines past the end read black */
    int  gray(int line, int pix) const;

    Fl_Image *img_;   /* scaled RGB copy, owned; null when empty */
    FaxImage  src_;   /* private copy for zoom/pan re-renders */
    int       disp_w_;
    int       empty_h_;
    uint8_t   pal_[256][3];   /* gray -> RGB lookup (core/palette.h) */

    /* zoom/pan state (docs/01 sec. 4 "Zoom/pan"; names follow the
     * trace: 5C/60 = viewport offsets, 64/68 = stored zoom-1 click
     * center, 6C/70 = zoom-2 pan) */
    int       zoom_;
    int       off5C_, off60_;
    int       v64_, v68_, v6C_, v70_;
    int       down_x_, down_y_;

    /* live mode: RGB columns, 500 px tall, up to 760 columns
     * (2280 lines / 3, the original's preview width) */
    bool      live_;
    int       live_cols_;     /* plotted columns                 */
    int       live_lines_;    /* lines fed so far (HUD "Line# n") */
    int       live_acc_[500]; /* tap sums for the current 3 lines */
    std::vector<uint8_t> live_rgb_;   /* 500*3 bytes per column */
};

#endif
