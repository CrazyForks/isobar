/* scopeview.cpp - see scopeview.h */
#include "scopeview.h"

#include <FL/fl_draw.H>
#include <FL/Enumerations.H>

#include <string.h>

/* display range: dBFS mapped onto the spectrum height / waterfall ramp.
 * (The original uses 20*ln(magnitude) - a quirk - with its own scale;
 * we keep our dBFS range, the ramp colors match.) */
static const double DB_TOP = 90.0;
static const double DB_BOT = -10.0;

/* frequency span drawn (docs/01 sec. 3.3: ~1157..2773 Hz, 3 bins/px
 * at 22050 Hz / 4096 bins = 5.383 Hz per bin) */
static const double FS_HZ   = 22050.0;
static const double BIN_HZ  = FS_HZ / FFT_N;
static const double F_LO    = 1157.0;
static const double F_HI    = 2773.0;

ScopeView::ScopeView(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h), have_(false), paused_(false)
{
    memset(hist_, 0, sizeof hist_);
    memset(wf_, 0, sizeof wf_);
}

/* strongest of the 3 FFT bins behind pixel column px, in dB */
static double col_db(const double db[FFT_BINS], int px)
{
    int bin0 = (int)(F_LO / BIN_HZ + 0.5);
    double m = DB_BOT;
    for (int j = 0; j < 3; j++) {
        int b = bin0 + 3 * px + j;
        if (b < FFT_BINS && db[b] > m)
            m = db[b];
    }
    return m;
}

/* normalize dB onto 0..1 for the display range */
static double norm_db(double m)
{
    double v = (m - DB_BOT) / (DB_TOP - DB_BOT);
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return v;
}

/* waterfall ramp (docs/01 sec. 3.3): black -> blue -> cyan -> white */
static void heat_color(double db, unsigned char *rgb)
{
    double v = norm_db(db);
    /* 3 segments: blue rises, then green (blue->cyan), then red
     * (cyan->white) */
    if (v < 1.0 / 3.0) {
        rgb[0] = 0;
        rgb[1] = 0;
        rgb[2] = (unsigned char)(v * 3.0 * 255.0);
    } else if (v < 2.0 / 3.0) {
        rgb[0] = 0;
        rgb[1] = (unsigned char)((v * 3.0 - 1.0) * 255.0);
        rgb[2] = 255;
    } else {
        rgb[0] = (unsigned char)((v * 3.0 - 2.0) * 255.0);
        rgb[1] = 255;
        rgb[2] = 255;
    }
}

/* spectrum trace colors (docs/01 sec. 3.3): newest near-white, older
 * traces fade through darker blues - level on the same ramp */
static void trace_color(int age, unsigned char *rgb)
{
    static const int level[5] = { 225, 180, 135, 90, 45 };   /* = HIST */
    if (age < 0) age = 0;
    if (age > 4) age = 4;
    double db = DB_BOT + (DB_TOP - DB_BOT) * level[age] / 255.0;
    heat_color(db, rgb);
}

void ScopeView::set_spectrum(const double mag_db[FFT_BINS])
{
    if (paused_)
        return;   /* click pause: freeze scope + waterfall */
    have_ = true;

    /* spectrum history: shift, newest at [0] */
    memmove(hist_[1], hist_[0], (HIST - 1) * WF_W * sizeof(double));
    for (int px = 0; px < WF_W; px++)
        hist_[0][px] = col_db(mag_db, px);

    /* waterfall: shift down one row, newest at row 0 (it scrolls by
     * hidden under the spectrum, like the original) */
    memmove(wf_[1], wf_[0], (WF_H - 1) * WF_W * 3);
    for (int px = 0; px < WF_W; px++)
        heat_color(hist_[0][px], wf_[0] + px * 3);

    redraw();
}

void ScopeView::clear()
{
    have_ = false;
    memset(hist_, 0, sizeof hist_);
    memset(wf_, 0, sizeof wf_);
    redraw();
}

int ScopeView::handle(int event)
{
    /* click pauses/resumes the scope (docs/01 sec. 3.3:
     * TForm1_Image3MouseUp) */
    if (event == FL_PUSH)
        return 1;
    if (event == FL_RELEASE) {
        paused_ = !paused_;
        redraw();
        return 1;
    }
    return Fl_Widget::handle(event);
}

/* x pixel of a frequency marker */
static int x_of_freq(ScopeView *s, double f)
{
    return s->x() + (int)((f - F_LO) / (F_HI - F_LO) * s->w() + 0.5);
}

void ScopeView::draw()
{
    /* black background */
    fl_color(0, 0, 0);
    fl_rectf(x(), y(), w(), h());

    /* gray marker lines at the 1500/2300 Hz subcarrier edges,
     * spectrum region only */
    fl_color(90, 90, 90);
    fl_line(x_of_freq(this, 1500.0), y(), x_of_freq(this, 1500.0),
            y() + SPEC_H - 1);
    fl_line(x_of_freq(this, 2300.0), y(), x_of_freq(this, 2300.0),
            y() + SPEC_H - 1);

    /* spectrum polylines: oldest first so the newest draws on top */
    if (have_) {
        for (int age = HIST - 1; age >= 0; age--) {
            unsigned char rgb[3];
            trace_color(age, rgb);
            fl_color(rgb[0], rgb[1], rgb[2]);
            for (int px = 1; px < w() && px < WF_W; px++) {
                int y1 = (int)(norm_db(hist_[age][px - 1]) * (SPEC_H - 1) + 0.5);
                int y2 = (int)(norm_db(hist_[age][px]) * (SPEC_H - 1) + 0.5);
                fl_line(x() + px - 1, y() + SPEC_H - 1 - y1,
                        x() + px,     y() + SPEC_H - 1 - y2);
            }
        }
    }

    /* waterfall rows (only SPEC_H..WF_H-1 are visible) */
    int dw = w() < WF_W ? w() : WF_W;
    int dh = h() - SPEC_H < WF_H - SPEC_H ? h() - SPEC_H : WF_H - SPEC_H;
    if (dh > 0)
        fl_draw_image(wf_[SPEC_H], x(), y() + SPEC_H, dw, dh, 3, WF_W * 3);

    /* paused: red band + label over the bottom rows (original: white
     * text on red, y ~96-105) */
    if (paused_) {
        fl_color(200, 30, 30);
        fl_rectf(x(), y() + h() - 10, w(), 10);
        fl_color(255, 255, 255);
        fl_font(FL_HELVETICA, 9);
        fl_draw("PAUSE", x(), y() + h() - 10, w(), 10, FL_ALIGN_CENTER);
    }
}
