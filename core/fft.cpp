/* fft.cpp - see fft.h. Iterative radix-2 DIT, bit-reversal permutation,
 * twiddles updated by complex rotation per butterfly. */

#include "fft.h"

#include <cmath>

static const double PI = 3.14159265358979323846;

void hann_window(double w[FFT_N])
{
    for (int i = 0; i < FFT_N; i++)
        w[i] = 0.5 - 0.5 * cos(2.0 * PI * i / FFT_N);
}

void fft_4096(const double in[FFT_N], double mag_db[FFT_BINS])
{
    double re[FFT_N], im[FFT_N], win[FFT_N];

    hann_window(win);

    /* window, then scatter into bit-reversed order (12 bits) */
    for (int i = 0; i < FFT_N; i++) {
        int r = 0;
        for (int b = 0; b < 12; b++)
            if (i & (1 << b))
                r |= 1 << (11 - b);
        re[r] = in[i] * win[i];
        im[r] = 0.0;
    }

    /* butterflies: sub-transform size doubles each pass */
    for (int len = 2; len <= FFT_N; len *= 2) {
        double step_r = cos(-2.0 * PI / len);   /* twiddle step W_len */
        double step_i = sin(-2.0 * PI / len);
        for (int i = 0; i < FFT_N; i += len) {
            double wr = 1.0, wi = 0.0;          /* running twiddle */
            for (int j = 0; j < len / 2; j++) {
                int a = i + j;
                int b = a + len / 2;
                double tr = re[b] * wr - im[b] * wi;
                double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                /* rotate twiddle to the next power of W_len */
                double nwr = wr * step_r - wi * step_i;
                wi = wr * step_i + wi * step_r;
                wr = nwr;
            }
        }
    }

    /* one-sided magnitude in dBFS. Hann coherent gain is 0.5, so a
     * full-scale sine (amplitude A) peaks at A*N/4; scale by 4/N so
     * 0 dB means "full-scale sine on this bin". */
    for (int k = 0; k < FFT_BINS; k++) {
        double m = sqrt(re[k] * re[k] + im[k] * im[k]) * 4.0 / FFT_N;
        mag_db[k] = 20.0 * log10(m + 1e-12);
    }
}
