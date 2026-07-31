/* filters.cpp - FIR design + streaming convolution. */

#include "filters.h"
#include <cmath>

static const double PI = 3.14159265358979323846;

void Fir::init(const std::vector<double> &taps)
{
    coef = taps;
    hist.assign(taps.size(), 0.0);
    head = 0;
}

double Fir::step(double x)
{
    int n = (int)coef.size();
    hist[head] = x;
    double acc = 0.0;
    int j = head;
    for (int k = 0; k < n; k++) {
        acc += coef[k] * hist[j];
        if (--j < 0)
            j = n - 1;
    }
    head++;
    if (head >= n)
        head = 0;
    return acc;
}

/* Hann window, length n. */
static double hann(int i, int n)
{
    return 0.5 - 0.5 * cos(2.0 * PI * i / (n - 1));
}

/* Ideal (unwindowed) lowpass tap at offset m from center, cutoff fc Hz. */
static double ideal_lp(int m, double fc, double fs)
{
    double fn = fc / fs;              /* normalized, cycles/sample */
    if (m == 0)
        return 2.0 * fn;
    return sin(2.0 * PI * fn * m) / (PI * m);
}

std::vector<double> design_lowpass(int ntaps, double fc, double fs)
{
    std::vector<double> h(ntaps);
    int mid = ntaps / 2;
    for (int i = 0; i < ntaps; i++)
        h[i] = ideal_lp(i - mid, fc, fs) * hann(i, ntaps);
    /* normalize to unity gain at DC */
    double sum = 0.0;
    for (int i = 0; i < ntaps; i++)
        sum += h[i];
    for (int i = 0; i < ntaps; i++)
        h[i] /= sum;
    return h;
}

std::vector<double> design_bandpass(int ntaps, double f1, double f2, double fs)
{
    std::vector<double> h(ntaps);
    int mid = ntaps / 2;
    for (int i = 0; i < ntaps; i++) {
        int m = i - mid;
        h[i] = (ideal_lp(m, f2, fs) - ideal_lp(m, f1, fs)) * hann(i, ntaps);
    }
    /* normalize to unity gain at passband center */
    double fc = 0.5 * (f1 + f2);
    double re = 0.0, im = 0.0;
    for (int i = 0; i < ntaps; i++) {
        double w = 2.0 * PI * fc / fs * i;
        re += h[i] * cos(w);
        im -= h[i] * sin(w);
    }
    double g = sqrt(re * re + im * im);
    for (int i = 0; i < ntaps; i++)
        h[i] /= g;
    return h;
}

std::vector<double> design_hilbert(int ntaps)
{
    std::vector<double> h(ntaps, 0.0);
    int mid = ntaps / 2;
    for (int i = 0; i < ntaps; i++) {
        int m = i - mid;
        if (m == 0)
            h[i] = 0.0;
        else
            h[i] = (1.0 - cos(PI * m)) / (PI * m);  /* 0 for even m */
        h[i] *= hann(i, ntaps);
    }
    return h;
}
