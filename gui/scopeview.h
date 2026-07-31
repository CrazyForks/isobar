/* scopeview.h - spectrum + waterfall scope widget.
 *
 * Maps docs/01-program-analysis.md section 3.3 (corrected 2026-07-30):
 * the 100x106 scope shows BOTH at once, like the original's Image3 -
 * a spectrum in the top 30 px (current trace + 4 fading history
 * traces, gray markers at 1500/2300 Hz) and a black->blue->cyan->
 * white waterfall scrolling through the bottom 76 rows (one row per
 * FFT block, newest entering at the top - hidden under the spectrum
 * for its first 30 updates, as in the original).
 *
 * Clicking the widget pauses/resumes the scope (red band + label),
 * the original's Image3MouseUp behavior.
 */
#ifndef ISOBAR_SCOPEVIEW_H
#define ISOBAR_SCOPEVIEW_H

#include "../core/fft.h"

#include <FL/Fl_Widget.H>

class ScopeView : public Fl_Widget {
public:
    ScopeView(int x, int y, int w, int h);

    /* New spectrum: FFT_BINS dBFS values (bins 0..2047).
     * UI thread only. Ignored while paused. */
    void set_spectrum(const double mag_db[FFT_BINS]);

    /* Back to the idle dark panel. */
    void clear();

    void draw() override;
    int  handle(int event) override;

private:
    static const int WF_W = 100;   /* scope columns (widget width)   */
    static const int WF_H = 106;   /* total rows (widget height)     */
    static const int SPEC_H = 30;  /* spectrum rows on top           */
    static const int HIST = 5;     /* spectrum traces (1 new + 4 old) */

    /* per-column spectrum values in dB, hist_[0] = newest */
    double hist_[HIST][WF_W];
    bool   have_;
    bool   paused_;

    /* waterfall scrollback: WF_H rows of packed RGB, row 0 = newest
     * (rows 0..SPEC_H-1 scroll by hidden under the spectrum) */
    unsigned char wf_[WF_H][WF_W * 3];
};

#endif
