/* decoder.h - WEFAX FM demodulator core.
 *
 * Maps docs/01-program-analysis.md section 3.2:
 *   int16 -> double
 *   51-tap bandpass FIR (1500..2300 Hz)        -> isolate subcarrier
 *   quadrature FM demod: 51-tap Hilbert -> Q, delay-line center tap -> I,
 *     phase = atan2(Q,I), unwrapped phase difference,
 *     subtract 2*pi*1500/fs (1500 Hz = black reference), scale x1102.25,
 *     clamp 0..255  (2300 Hz -> ~251 = white)
 *   51-tap lowpass FIR smoothing
 *   decimate 22050 -> 8000 samples/s
 *
 * Unlike the original GUI program this runs as a plain stream (the 100 ms
 * block structure there is an artifact of the Windows audio callback; the
 * math is identical).
 */
#ifndef ISOBAR_DECODER_H
#define ISOBAR_DECODER_H

#include "filters.h"
#include <vector>
#include <cstdint>

struct FmDecoder {
    static const double FS_IN;    /* 22050 Hz */
    static const double FS_OUT;   /*  8000 Hz */

    Fir bpf;    /* 51-tap bandpass 1500..2300 Hz */
    Fir hilb;   /* 51-tap Hilbert transformer     */
    Fir lpf;    /* 51-tap lowpass (video smoothing) */

    /* I channel: plain delay line matching the Hilbert group delay (25). */
    double delay[51];
    int dpos;

    double bpf_out;     /* last bandpass output, for the scope tap */

    double prev_phase;
    bool have_phase;

    /* rational resampler 22050 -> 8000 (= 160/441), linear interpolation */
    double next_out;    /* input-sample position of next output sample */
    double prev_lpf;    /* previous LPF output, for interpolation */

    FmDecoder();
    void reset();

    /* Feed one 22050 Hz sample (int16 range); appends 0 or more 8000 S/s
     * video bytes (0=black .. 255=white) to `out`. */
    void feed(double x, std::vector<uint8_t> &out);
};

/* Convenience: decode a whole buffer of 22050 Hz int16-range samples.
 * `progress` (may be null) is called every ~10 s of input.
 * `block` (may be null) is called with each consecutive 4096-sample
 * block of bandpass-filtered audio, for the spectrum scope
 * (docs/01-program-analysis.md section 3.3).
 * `tone` (may be null) is called every 100 ms of video with the
 * 300/450 Hz tone levels (docs/01 section 3.2(5), core/tonedetect.h). */
std::vector<uint8_t> fm_decode(const std::vector<double> &samples,
                               void (*progress)(double sec_done, double sec_total),
                               void (*block)(const double bpf[4096]) = nullptr,
                               void (*tone)(double level300, double level450) = nullptr);

/* 60 rpm mode (docs/01-program-analysis.md sec. 3.2(6)): 60-rpm lines
 * are 1.0 s, so the original runs the video stream at 4000 S/s and
 * keeps every downstream constant (4000-sample lines, sync windows)
 * unchanged. This converts an 8000 S/s video stream the same way:
 * average sample pairs. Feed the result to scan_lines as-is. */
std::vector<uint8_t> video_halve_rate(const std::vector<uint8_t> &video);

#endif
