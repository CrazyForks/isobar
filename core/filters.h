/* filters.h - windowed-sinc FIR design and a simple streaming FIR filter.
 *
 * All filters are designed fresh here from textbook windowed-sinc formulas
 * (no tap tables copied from anywhere). Sampling rate for the decoder
 * chain is 22050 Hz (see docs/01-program-analysis.md section 3).
 */
#ifndef ISOBAR_FILTERS_H
#define ISOBAR_FILTERS_H

#include <vector>

/* A streaming FIR filter (direct form, circular history buffer). */
struct Fir {
    std::vector<double> coef;  /* taps */
    std::vector<double> hist;  /* circular buffer, same length as coef */
    int head;                  /* write position in hist */

    void init(const std::vector<double> &taps);
    double step(double x);     /* push one sample, return filtered output */
};

/* Design helpers. ntaps should be odd (type-I FIR), fs in Hz. */

/* Lowpass: cutoff fc Hz, Hann window. */
std::vector<double> design_lowpass(int ntaps, double fc, double fs);

/* Bandpass: passband f1..f2 Hz (difference of two lowpasses), Hann window. */
std::vector<double> design_bandpass(int ntaps, double f1, double f2, double fs);

/* Hilbert transformer (odd-length, antisymmetric), Hann window.
 * Group delay is (ntaps-1)/2 samples. */
std::vector<double> design_hilbert(int ntaps);

#endif
