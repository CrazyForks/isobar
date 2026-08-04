/* tonedetect.h - WEFAX 300/450 Hz start/stop tone detection.
 *
 * Maps docs/01-program-analysis.md section 3.2(5): sign of the video
 * signal -> +-10000, fed into two 2-pole resonators (300 Hz start,
 * 450 Hz stop, 10 Hz bandwidth); output squared + lowpassed; block max
 * is the tone level. Level > 1e7 for DetTime consecutive blocks means
 * "tone present" (the original's auto start/stop; here it drives the
 * Control LED and, later, auto-record).
 *
 * One deliberate adaptation: the original runs the resonators on the
 * 22050 Hz demod signal before decimation; we run them on the final
 * 8000 S/s video bytes. The tones (300/450 Hz) pass the decimation
 * unchanged, so the detection result is the same; coefficients are
 * re-derived with the documented formulas at fs=8000.
 */
#ifndef ISOBAR_TONEDETECT_H
#define ISOBAR_TONEDETECT_H

struct ToneDetect {
    /* 2-pole resonator, one per tone */
    struct Resonator {
        double b0, a1, a2;   /* H(z) = b0 / (1 - a1 z^-1 - a2 z^-1) */
        double y1, y2;       /* past outputs                        */
        /* envelope LPF (documented taps from the original) */
        double x1, x2;       /* past squared inputs                 */
        double e1, e2;       /* past envelope outputs               */
        double block_max;    /* max envelope this block             */
    };

    Resonator r300, r450;
    int block_n;   /* samples into the current 100 ms block */

    static const int FS = 8000;
    static const int BLOCK = 800;          /* 100 ms */
    static const double LEVEL_TH;          /* "tone present" threshold */

    ToneDetect();

    /* Feed one 8000 S/s video byte. Every 800 samples (100 ms) returns
     * true and sets level300/level450 to that block's levels. */
    bool feed(unsigned char v, double &level300, double &level450);
};

#endif
