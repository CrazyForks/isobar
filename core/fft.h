/* fft.h - 4096-point FFT for the spectrum scope.
 *
 * Maps docs/01-program-analysis.md section 3.3:
 *   4096-pt radix-2 DIT FFT; input is BPF output x Hann window;
 *   spectrum drawn as 20*log10 magnitudes.
 * Fresh textbook Cooley-Tukey implementation; nothing taken from the
 * original program (its twiddle/table layout is irrelevant here).
 */
#ifndef ISOBAR_FFT_H
#define ISOBAR_FFT_H

static const int FFT_N    = 4096;   /* transform size */
static const int FFT_BINS = 2048;   /* usable bins (one-sided) */

/* Apply a Hann window to in[], transform, and write the magnitude of
 * bins 0..2047 to mag_db[] in dBFS (0 dB = full-scale sine centered on
 * a bin; silence floors at -240 dB). Sample rate is whatever the
 * caller's data is (22050 Hz for the decoder's BPF output). */
void fft_4096(const double in[FFT_N], double mag_db[FFT_BINS]);

#endif
