/* demod-test - synthetic fm_decode unit test (core/decoder.cpp).
 * Registered with ctest; run via `ctest --test-dir build -R demod-test`.
 *
 * (audit 2026-08-06 sec. 3.5.3) A pure tone at the WEFAX black frequency
 * (1500 Hz) must decode to ~0 and one at the white frequency (2300 Hz) to
 * ~251, per the mapping clamp(1102.25*(w - 0.42744), 0, 255) with
 * w = 2*pi*f/22050 (docs/01 sec. 3.2). Localizes demod-vs-sync regressions:
 * every other test exercises both through a real recording.
 */
#include "../core/decoder.h"

#include <cmath>
#include <cstdio>
#include <vector>

/* Local PI — see core/tonedetect.cpp (M_PI is missing under MSVC). */
static const double PI = 3.14159265358979323846;

static int fail(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

/* mean of the video bytes, skipping the filter/demod settling head */
static double settled_mean(const std::vector<uint8_t> &v)
{
    size_t skip = v.size() / 2;
    long s = 0;
    for (size_t i = skip; i < v.size(); i++)
        s += v[i];
    return (double)s / (double)(v.size() - skip);
}

int main()
{
    const double fs = 22050.0;
    const int n = (int)fs;               /* 1 s of audio -> ~8000 video */
    std::vector<double> samples(n);

    /* 1500 Hz tone -> black (~0) */
    for (int i = 0; i < n; i++)
        samples[i] = 12000.0 * sin(2.0 * PI * 1500.0 * i / fs);
    double black = settled_mean(fm_decode(samples, nullptr));
    if (black > 10.0)
        return fail("1500 Hz tone does not decode to black");

    /* 2300 Hz tone -> white (~251) */
    for (int i = 0; i < n; i++)
        samples[i] = 12000.0 * sin(2.0 * PI * 2300.0 * i / fs);
    double white = settled_mean(fm_decode(samples, nullptr));
    if (white < 240.0 || white > 255.0)
        return fail("2300 Hz tone does not decode to white");

    printf("demod-test: PASS (black %.1f, white %.1f)\n", black, white);
    return 0;
}
