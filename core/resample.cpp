/* resample.cpp - see resample.h. */

#include "resample.h"

void Resampler::init(double fs_in, double fs_out)
{
    /* cutoff below the LOWER of the two Nyquists: anti-alias when
     * downsampling, anti-image when upsampling */
    double fc = (fs_in < fs_out ? fs_in : fs_out) * 0.45;
    lpf.init(design_lowpass(63, fc, fs_in));
    step_in = fs_in / fs_out;
    next_out = 0.0;
    prev = 0.0;
}

void Resampler::feed(double x, std::vector<double> &out)
{
    double y = lpf.step(x);
    /* same pick-off as FmDecoder's decimation stage */
    while (next_out <= 0.0) {
        double frac = 1.0 + next_out;   /* next_out in (-1, 0] here */
        out.push_back(prev + (y - prev) * frac);
        next_out += step_in;
    }
    next_out -= 1.0;
    prev = y;
}
