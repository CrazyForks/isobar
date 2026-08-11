/* ratefit.cpp - see ratefit.h. */

#include "ratefit.h"

#include <cmath>

namespace {

const int LINE_SAMPLES = 4000;
const int NBINS = 2000;        /* fold bins: 2 video samples each. Finer
                                  buys nothing - the video is already
                                  lowpassed by FmDecoder - and costs
                                  emptier bins on short receptions. */

/* Variance of the folded profile at trial period P. This is the whole
 * measurement: it peaks when every line's vertical features land in the
 * same bins. Phase is walked incrementally rather than by fmod per
 * sample - the sweep touches every sample a few hundred times. */
double fold_var(const std::vector<uint8_t> &v, double P)
{
    std::vector<double> sum((size_t)NBINS, 0.0);
    std::vector<long>   cnt((size_t)NBINS, 0);

    const double scale = (double)NBINS / P;
    double pos = 0.0;
    for (size_t i = 0; i < v.size(); i++) {
        int b = (int)(pos * scale);
        if (b >= NBINS)          /* only reachable through rounding */
            b = NBINS - 1;
        sum[(size_t)b] += v[i];
        cnt[(size_t)b]++;
        pos += 1.0;
        if (pos >= P)
            pos -= P;
    }

    double total = 0.0;
    long n = 0;
    for (int b = 0; b < NBINS; b++) {
        total += sum[(size_t)b];
        n += cnt[(size_t)b];
    }
    if (n == 0)
        return 0.0;
    const double gm = total / (double)n;

    double var = 0.0;
    int used = 0;
    for (int b = 0; b < NBINS; b++) {
        if (cnt[(size_t)b] == 0)
            continue;
        const double m = sum[(size_t)b] / (double)cnt[(size_t)b];
        var += (m - gm) * (m - gm);
        used++;
    }
    return used ? var / (double)used : 0.0;
}

}  /* namespace */

RateFit fit_line_period(const std::vector<uint8_t> &video)
{
    RateFit r;
    r.period = (double)LINE_SAMPLES;
    r.ppm = 0.0;
    r.halfw = 0.0;
    r.skew_px = 0.0;
    r.prom = 0.0;
    r.ok = false;

    if (video.size() < (size_t)RATEFIT_MIN_LINES * LINE_SAMPLES)
        return r;

    /* Coarse pass: half-sample steps across the whole sweep. The fold
     * score varies smoothly on this scale, so a coarse peak cannot be
     * more than one step away from the true one. The mean over this
     * sweep is the "no particular period" level that prom compares to. */
    double best = -1.0, bestP = (double)LINE_SAMPLES;
    double sum = 0.0;
    int n = 0;
    for (double P = RATEFIT_MIN_PERIOD; P <= RATEFIT_MAX_PERIOD + 1e-9;
         P += 0.5) {
        const double s = fold_var(video, P);
        sum += s;
        n++;
        if (s > best) {
            best = s;
            bestP = P;
        }
    }
    const double mean = n ? sum / (double)n : 0.0;

    /* Fine pass: hundredth-sample steps either side of the coarse peak.
     * 0.01 samples is 2.5 ppm, well below anything visible. */
    const double lo = bestP - 0.6, hi = bestP + 0.6;
    double fbest = -1.0, fbestP = bestP;
    std::vector<std::pair<double, double> > curve;
    for (double P = lo; P <= hi + 1e-9; P += 0.01) {
        const double s = fold_var(video, P);
        curve.push_back(std::make_pair(P, s));
        if (s > fbest) {
            fbest = s;
            fbestP = P;
        }
    }

    /* Sharpness: how far from the peak the score still holds 90% of its
     * maximum. A real peak falls away quickly; a picture that does not
     * determine the rate gives a curve that is flat across the window,
     * and then halfw runs out to the window edge. */
    double halfw = 0.0;
    for (size_t i = 0; i < curve.size(); i++)
        if (curve[i].second >= 0.9 * fbest) {
            const double d = std::fabs(curve[i].first - fbestP);
            if (d > halfw)
                halfw = d;
        }

    const double lines = (double)video.size() / (double)LINE_SAMPLES;

    r.period = fbestP;
    r.ppm = (fbestP - (double)LINE_SAMPLES) / (double)LINE_SAMPLES * 1e6;
    r.halfw = halfw;
    /* what the uncertainty costs: halfw samples per line, over `lines`
     * lines, at 1500 px per 4000 samples */
    r.skew_px = halfw * lines * 3.0 / 8.0;
    r.prom = mean > 0.0 ? fbest / mean : 0.0;
    r.ok = r.skew_px <= RATEFIT_MAX_SKEW_PX && r.prom >= RATEFIT_MIN_PROM;
    return r;
}

std::vector<uint8_t> video_retime(const std::vector<uint8_t> &video,
                                  double period)
{
    std::vector<uint8_t> out;
    if (video.empty() || period <= 0.0)
        return out;

    /* Output sample k reads input position k * period/4000: `period`
     * input samples then span exactly 4000 output ones. */
    const double step = period / (double)LINE_SAMPLES;
    const size_t n = (size_t)((double)(video.size() - 1) / step) + 1;
    out.reserve(n);

    for (size_t k = 0; k < n; k++) {
        const double x = (double)k * step;
        const size_t i = (size_t)x;
        if (i + 1 >= video.size()) {
            out.push_back(video.back());
            continue;
        }
        const double f = x - (double)i;
        out.push_back((uint8_t)(video[i] * (1.0 - f) + video[i + 1] * f + 0.5));
    }
    return out;
}
