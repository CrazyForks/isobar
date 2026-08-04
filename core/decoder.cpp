/* decoder.cpp - see decoder.h for the pipeline description. */

#include "decoder.h"
#include "tonedetect.h"
#include <cmath>
#include <cstddef>   /* size_t — older libstdc++ doesn't leak it via <vector> */

static const double PI = 3.14159265358979323846;

const double FmDecoder::FS_IN  = 22050.0;
const double FmDecoder::FS_OUT = 8000.0;

/* Video scaling from the spec: 2300 Hz maps to ~251. */
static const double PHASE_REF = 2.0 * PI * 1500.0 / 22050.0; /* ~0.42744 rad */
static const double SCALE     = 1102.25;

FmDecoder::FmDecoder()
{
    bpf.init(design_bandpass(51, 1500.0, 2300.0, FS_IN));
    hilb.init(design_hilbert(51));
    lpf.init(design_lowpass(51, 1200.0, FS_IN));
    reset();
}

void FmDecoder::reset()
{
    for (int i = 0; i < 51; i++)
        delay[i] = 0.0;
    dpos = 0;
    prev_phase = 0.0;
    have_phase = false;
    next_out = 0.0;
    prev_lpf = 0.0;
}

void FmDecoder::feed(double x, std::vector<uint8_t> &out)
{
    /* 1. bandpass: isolate the 1500..2300 Hz subcarrier */
    double b = bpf.step(x);
    bpf_out = b;

    /* 2. quadrature pair: I from delay-line center tap, Q from Hilbert */
    delay[dpos] = b;
    dpos++;
    if (dpos >= 51)
        dpos = 0;
    double i_ch = delay[(dpos + 51 - 26) % 51]; /* 25-sample group delay */
    double q_ch = hilb.step(b);

    /* 3. FM demod: phase difference with 2*pi unwrap */
    double phase = atan2(q_ch, i_ch);
    double video = 0.0;
    if (have_phase) {
        double d = phase - prev_phase;
        while (d > PI)
            d -= 2.0 * PI;
        while (d < -PI)
            d += 2.0 * PI;
        /* reference to 1500 Hz black carrier, scale, clamp to 0..255 */
        double v = (d - PHASE_REF) * SCALE;
        if (v < 0.0)
            v = 0.0;
        if (v > 255.0)
            v = 255.0;
        video = v;
    }
    prev_phase = phase;
    have_phase = true;

    /* 4. lowpass smoothing of the video signal */
    double s = lpf.step(video);

    /* 5. decimate 22050 -> 8000 (factor 160/441), linear interpolation.
     *    next_out is the position of the next output sample relative to
     *    the current input sample (<= 0 means an output falls here). */
    while (next_out <= 0.0) {
        double frac = 1.0 + next_out;       /* next_out in (-1, 0] here */
        double v = prev_lpf + (s - prev_lpf) * frac;
        int iv = (int)(v + 0.5);
        if (iv < 0)
            iv = 0;
        if (iv > 255)
            iv = 255;
        out.push_back((uint8_t)iv);
        next_out += FS_IN / FS_OUT;         /* 2.75625 input samples */
    }
    next_out -= 1.0;
    prev_lpf = s;
}

std::vector<uint8_t> fm_decode(const std::vector<double> &samples,
                               void (*progress)(double, double),
                               void (*block)(const double[4096]),
                               void (*tone)(double, double))
{
    FmDecoder dec;
    std::vector<uint8_t> out;
    out.reserve((size_t)(samples.size() * FmDecoder::FS_OUT / FmDecoder::FS_IN) + 16);

    /* scope tap: collect BPF output into 4096-sample blocks */
    double blk[4096];
    int bn = 0;

    /* tone tap: 300/450 Hz levels over each 100 ms of video */
    ToneDetect tones;
    size_t vi = 0;   /* video bytes already fed to the detectors */
    double l300, l450;

    long total = (long)samples.size();
    long tick = (long)(10.0 * FmDecoder::FS_IN); /* report every 10 s */
    for (long i = 0; i < total; i++) {
        dec.feed(samples[i], out);
        if (block) {
            blk[bn++] = dec.bpf_out;
            if (bn == 4096) {
                block(blk);
                bn = 0;
            }
        }
        if (tone) {
            for (; vi < out.size(); vi++)
                if (tones.feed(out[vi], l300, l450))
                    tone(l300, l450);
        }
        if (progress && (i % tick) == tick - 1)
            progress((double)(i + 1) / FmDecoder::FS_IN,
                     (double)total / FmDecoder::FS_IN);
    }
    return out;
}

std::vector<uint8_t> video_halve_rate(const std::vector<uint8_t> &video)
{
    std::vector<uint8_t> out;
    out.reserve(video.size() / 2);
    for (size_t i = 0; i + 1 < video.size(); i += 2)
        out.push_back((uint8_t)((video[i] + video[i + 1] + 1) / 2));
    return out;
}
