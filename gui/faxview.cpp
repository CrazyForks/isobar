/* faxview.cpp - see faxview.h */
#include "faxview.h"

#include <FL/Fl.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_draw.H>

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* live preview geometry (docs/01 sec. 4): the 1500 samples compress
 * to 500 px vertically (3:1), and one column is plotted per 3 LINES
 * (3-line average) - 1/3 scale in both axes, aspect preserved */
static const int LIVE_H = 500;
static const int LIVE_MAX_COLS = 760;   /* 2280 lines / 3 */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

FaxView::FaxView(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h), img_(0), disp_w_(w), empty_h_(h),
      zoom_(0), off5C_(0), off60_(0),
      v64_(253), v68_(166), v6C_(0), v70_(0),
      down_x_(0), down_y_(0),
      live_(false), live_cols_(0),
      live_click_cb_(0), live_click_ud_(0)
{
    for (int i = 0; i < 256; i++)
        pal_[i][0] = pal_[i][1] = pal_[i][2] = (uint8_t)i;
}

FaxView::~FaxView()
{
    delete img_;
}

void FaxView::set_palette(const uint8_t pal[256][3])
{
    memcpy(pal_, pal, sizeof pal_);
}

int FaxView::gray(int line, int pix) const
{
    if (line < 0 || line >= (int)src_.lines.size())
        return 0;
    return src_.lines[line][pix];
}

void FaxView::set_image(const FaxImage &img)
{
    src_ = img;
    render();
}

void FaxView::reset_zoom()
{
    zoom_ = 0;
    off5C_ = 0;
    off60_ = 0;
}

void FaxView::dev_zoom_to(int level)
{
    while (zoom_ < level && !src_.lines.empty())
        zoom_in(380, 250);
}

/* left click = zoom in, centered on the click (kgfax.exe.c
 * 8498-8578); clamped at level 2 with no re-render past the top */
void FaxView::zoom_in(int cx, int cy)
{
    if (++zoom_ > 2) {
        zoom_ = 2;
        return;
    }
    if (zoom_ == 1) {
        v64_ = clampi(cx, 253, 506);
        v68_ = clampi(cy, 166, 333);
        off5C_ = clampi((int)(cx * 1.5) - 380, 0, 379);
        off60_ = clampi((int)(cy * 1.5) - 250, 0, 249);
    } else {   /* zoom_ == 2 */
        off5C_ = clampi(3 * v64_ + 2 * cx - 1140, 0, 1519);
        off60_ = clampi(3 * v68_ + 2 * cy - 750, 0, 999);
        v6C_ = off5C_;
        v70_ = off60_;
    }
    render();
}

/* right click = zoom out (8443-8476); clamped at level 0 with no
 * re-render at the bottom */
void FaxView::zoom_out()
{
    if (zoom_ == 0)
        return;
    if (--zoom_ == 1) {
        off5C_ = clampi((int)(v64_ * 1.5) - 380, 0, 379);
        off60_ = clampi((int)(v68_ * 1.5) - 250, 0, 249);
    }
    render();
}

/* left drag = pan (8690-8735, 8816-8843); deltas are down - up.
 * Level 0 has no pan (the original's level-0 drag only moves the
 * rotate window, which our rotate does not port). */
void FaxView::pan(int dx, int dy)
{
    if (zoom_ == 1) {
        v64_ = clampi(v64_ + (int)(dx * 0.6666666), 253, 506);
        v68_ = clampi(v68_ + (int)(dy * 0.6666666), 166, 333);
        off5C_ = clampi((int)(v64_ * 1.5) - 380, 0, 379);
        off60_ = clampi((int)(v68_ * 1.5) - 250, 0, 249);
    } else if (zoom_ == 2) {
        v6C_ = clampi(v6C_ + dx, 0, 1519);
        v70_ = clampi(v70_ + dy, 0, 999);
        v64_ = (v6C_ + 300) / 3;
        v68_ = (v70_ + 250) / 3;
        off5C_ = v6C_;
        off60_ = v70_;
    } else {
        return;
    }
    render();
}

void FaxView::set_live_click_cb(void (*cb)(int y, void *ud), void *ud)
{
    live_click_cb_ = cb;
    live_click_ud_ = ud;
}

int FaxView::handle(int ev)
{
    /* While receiving, a mouse-up is the manual sync align, not
     * zoom/pan (docs/01 sec. 4 "Zoom/pan"): the original takes it
     * straight off MouseUp with no click-vs-drag and no button test,
     * so neither is applied here either. */
    if (live_) {
        if (ev == FL_PUSH)
            return 1;         /* accept, or FLTK sends us no RELEASE */
        if (ev == FL_RELEASE) {
            if (live_click_cb_)
                live_click_cb_(Fl::event_y() - y(), live_click_ud_);
            return 1;
        }
        return Fl_Widget::handle(ev);
    }
    /* zoom/pan only when idle and an image is present, like the
     * original (docs/01 sec. 4 "Zoom/pan") */
    if (src_.lines.empty())
        return Fl_Widget::handle(ev);
    if (ev == FL_PUSH) {
        down_x_ = Fl::event_x() - x();
        down_y_ = Fl::event_y() - y();
        return 1;
    }
    if (ev == FL_RELEASE) {
        int ex = Fl::event_x() - x();
        int ey = Fl::event_y() - y();
        int dx = down_x_ - ex;   /* deltas are down - up (8431-8432) */
        int dy = down_y_ - ey;
        if (abs(dx) < 10 && abs(dy) < 10) {
            if (Fl::event_button() == FL_RIGHT_MOUSE)
                zoom_out();
            else
                zoom_in(ex, ey);
        } else if (Fl::event_button() == FL_LEFT_MOUSE) {
            pan(dx, dy);
        }
        return 1;
    }
    return Fl_Widget::handle(ev);
}

void FaxView::render()
{
    delete img_;
    img_ = 0;

    const int nlines = (int)src_.lines.size();
    if (nlines <= 0) {
        size(disp_w_, empty_h_);   /* blank 760x500 panel again */
        redraw();
        return;
    }

    /* Column view in all zoom levels (docs/01 sec. 4 "Zoom/pan";
     * kgfax.exe.c 8933-9021): screen x = line axis, y = pixel axis,
     * line start at the bottom; only the sampling changes. Columns
     * past the last line stay black, like the original's zeroed
     * buffer. Fixed 760x500 bitmap. */
    std::vector<uint8_t> rgb((size_t)LIVE_MAX_COLS * LIVE_H * 3, 0);
    for (int sx = 0; sx < LIVE_MAX_COLS; sx++) {
        if (zoom_ == 0) {
            /* 1/3: screen col sx averages lines 3sx..3sx+2, screen row
             * sy averages pixels 3*(499-sy)..+2 (the .syn-load redraw
             * geometry, 9403-9431) */
            int l0 = 3 * sx;
            if (l0 >= nlines)
                break;
            int nl = nlines - l0;
            if (nl > 3)
                nl = 3;
            for (int sy = 0; sy < LIVE_H; sy++) {
                int p0 = 3 * (LIVE_H - 1 - sy);
                int sum = 0;
                for (int dl = 0; dl < nl; dl++) {
                    const uint8_t *row = src_.lines[l0 + dl].data();
                    sum += (int)row[p0] + row[p0 + 1] + row[p0 + 2];
                }
                uint8_t v = (uint8_t)(sum / (3 * nl));
                const uint8_t *c = pal_[v];
                uint8_t *px = &rgb[((size_t)sy * LIVE_MAX_COLS + sx) * 3];
                px[0] = c[0];
                px[1] = c[1];
                px[2] = c[2];
            }
        } else if (zoom_ == 1) {
            /* 1/2: 2x2 box average of lines 2*(5C+sx)+{0,1} at pixels
             * 2*(749-60-sy)+{0,1}, per-line /2 then /2 (8933-8960) */
            int l0 = 2 * (off5C_ + sx);
            if (l0 >= nlines)
                break;
            for (int sy = 0; sy < LIVE_H; sy++) {
                int p0 = 2 * (749 - off60_ - sy);
                int a = (gray(l0, p0) + gray(l0, p0 + 1)) / 2;
                int b = (gray(l0 + 1, p0) + gray(l0 + 1, p0 + 1)) / 2;
                uint8_t v = (uint8_t)((a + b) / 2);
                const uint8_t *c = pal_[v];
                uint8_t *px = &rgb[((size_t)sy * LIVE_MAX_COLS + sx) * 3];
                px[0] = c[0];
                px[1] = c[1];
                px[2] = c[2];
            }
        } else {
            /* 1:1: line 5C+sx, pixel 1499-60-sy, no averaging
             * (8971-8987) */
            int l = off5C_ + sx;
            if (l >= nlines)
                break;
            for (int sy = 0; sy < LIVE_H; sy++) {
                uint8_t v = (uint8_t)gray(l, 1499 - off60_ - sy);
                const uint8_t *c = pal_[v];
                uint8_t *px = &rgb[((size_t)sy * LIVE_MAX_COLS + sx) * 3];
                px[0] = c[0];
                px[1] = c[1];
                px[2] = c[2];
            }
        }
    }

    /* `tmp` only borrows rgb; copy() makes an owning image, so rgb can
     * die at end of scope */
    Fl_RGB_Image tmp(rgb.data(), LIVE_MAX_COLS, LIVE_H, 3);
    img_ = tmp.copy();

    size(LIVE_MAX_COLS, LIVE_H);
    redraw();
}

void FaxView::live_begin()
{
    live_ = true;
    live_cols_ = 0;
    live_lines_ = 0;
    memset(live_acc_, 0, sizeof live_acc_);
    /* row-major, fixed stride: pixel (r,c) at (r*LIVE_MAX_COLS + c)*3,
     * so one Fl_RGB_Image can draw the whole frontier */
    live_rgb_.assign((size_t)LIVE_H * LIVE_MAX_COLS * 3, 0);
    size(disp_w_, empty_h_);
    redraw();
}

void FaxView::live_column(const uint8_t *line1500)
{
    if (!live_ || live_cols_ >= LIVE_MAX_COLS)
        return;
    /* accumulate the 3 px -> 1 tap values; every 3rd line plots one
     * column of the 3-line average (docs/01 sec. 4) */
    for (int y = 0; y < LIVE_H; y++)
        live_acc_[y] += ((int)line1500[y * 3] + line1500[y * 3 + 1] +
                         line1500[y * 3 + 2]) / 3;
    live_lines_++;
    if (live_lines_ % 3 != 0)
        return;

    /* plot the column, FLIPPED: line start (sync edge) at the bottom,
     * like the original (docs/01 sec. 4, y = 499 - tap) */
    for (int y = 0; y < LIVE_H; y++) {
        uint8_t v = (uint8_t)(live_acc_[y] / 3);
        live_acc_[y] = 0;
        uint8_t *px = &live_rgb_[((size_t)(LIVE_H - 1 - y) * LIVE_MAX_COLS +
                                  live_cols_) * 3];
        const uint8_t *c = pal_[v];
        px[0] = c[0];
        px[1] = c[1];
        px[2] = c[2];
    }
    live_cols_++;
    if (live_cols_ > w())
        size(live_cols_, LIVE_H);   /* widen: scrollbars appear */
    /* keep the frontier in view */
    Fl_Scroll *sc = (Fl_Scroll *)parent();
    if (sc && live_cols_ > sc->w())
        sc->scroll_to(live_cols_ - sc->w(), sc->yposition());
    redraw();
}

void FaxView::live_end(const FaxImage &img)
{
    live_ = false;
    live_rgb_.clear();
    live_rgb_.shrink_to_fit();
    set_image(img);
}

void FaxView::draw()
{
    if (live_) {
        /* black reception panel + received columns in one image */
        fl_color(FL_BLACK);
        fl_rectf(x(), y(), w(), h());
        if (live_cols_ > 0) {
            Fl_RGB_Image img(live_rgb_.data(), live_cols_, LIVE_H, 3,
                             LIVE_MAX_COLS * 3);
            img.draw(x(), y());
            /* progress cursor: cyan frontier line + top tick
             * (the original's reverse-L) and the HUD text */
            int fx = x() + live_cols_;
            fl_color(fl_rgb_color(0, 200, 200));
            fl_line(fx, y(), fx, y() + LIVE_H - 1);
            fl_line(fx, y(), fx + 8, y());
            fl_font(FL_HELVETICA, 11);
            fl_draw("Scanning", fx + 4, y() + 12);
            char buf[32];
            snprintf(buf, sizeof buf, "Line# %d", live_lines_);
            fl_draw(buf, fx + 4, y() + 26);
        }
        return;
    }
    if (img_) {
        img_->draw(x(), y());
    } else {
        fl_color(60, 60, 60);
        fl_rectf(x(), y(), w(), h());
        fl_color(FL_WHITE);
        fl_draw("no image loaded", x(), y(), w(), h(), FL_ALIGN_CENTER);
    }
}
