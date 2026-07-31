/* tonedetect.cpp - see tonedetect.h. */

#include "tonedetect.h"

#include <cmath>

/* Local PI (not M_PI — that's missing under MSVC unless _USE_MATH_DEFINES
 * is set, so the rest of the codebase uses this local constant instead). */
static const double PI = 3.14159265358979323846;

/* The original's threshold is 1e7 at fs=22050 with its own scaling.
 * At fs=8000 our levels sit differently, so the threshold was measured:
 * a full-scale tone gives ~1.3e10; flat DC and cross-tone leakage stay
 * below ~3e7, and the worst image content in the JMH sample (the test
 * chart's resolution wedges, which oscillate near 300/450 Hz) peaks at
 * ~1.5e9. 3e9 sits between: wedges never cross, a half-amplitude tone
 * (~3.3e9) still does. */
const double ToneDetect::LEVEL_TH = 3e9;

/* resonator coefficients, formulas from docs/01 sec. 3.2(5):
 *   a1 = cos(2 pi f / fs) * 2 e^(-pi BW / fs)
 *   a2 = -e^(-2 pi BW / fs)
 *   b0 = sin(2 pi f / fs) * 0.1
 * with BW = 10 Hz; the original's fs=22050, ours 8000 (see header). */
static void resonator_init(ToneDetect::Resonator &r, double freq)
{
    const double fs = ToneDetect::FS;
    const double bw = 10.0;
    const double w = 2.0 * PI * freq / fs;
    r.a1 = cos(w) * 2.0 * exp(-PI * bw / fs);
    r.a2 = -exp(-2.0 * PI * bw / fs);
    r.b0 = sin(w) * 0.1;
    r.y1 = r.y2 = 0;
    r.x1 = r.x2 = 0;
    r.e1 = r.e2 = 0;
    r.block_max = 0;
}

ToneDetect::ToneDetect()
{
    resonator_init(r300, 300.0);
    resonator_init(r450, 450.0);
    block_n = 0;
}

void ToneDetect::reset()
{
    *this = ToneDetect();
}

/* envelope LPF: documented taps (docs/01 sec. 3.2(5)) */
static double envelope(ToneDetect::Resonator &r, double sq)
{
    const double B0 = 0.00019897, B1 = 0.00039794, B2 = 0.00019897;
    const double A1 = -1.95970703, A2 = 0.96050292;
    double e = B0 * sq + B1 * r.x1 + B2 * r.x2 - A1 * r.e1 - A2 * r.e2;
    r.x2 = r.x1;
    r.x1 = sq;
    r.e2 = r.e1;
    r.e1 = e;
    return e;
}

static void resonator_feed(ToneDetect::Resonator &r, double x)
{
    double y = r.b0 * x + r.a1 * r.y1 + r.a2 * r.y2;
    r.y2 = r.y1;
    r.y1 = y;
    double e = envelope(r, y * y);
    if (e > r.block_max)
        r.block_max = e;
}

bool ToneDetect::feed(unsigned char v, double &level300, double &level450)
{
    /* sign(video - mid) -> +-10000, as documented */
    double x = v >= 128 ? 10000.0 : -10000.0;
    resonator_feed(r300, x);
    resonator_feed(r450, x);

    if (++block_n < BLOCK)
        return false;
    block_n = 0;
    level300 = r300.block_max;
    level450 = r450.block_max;
    r300.block_max = 0;
    r450.block_max = 0;
    return true;
}
