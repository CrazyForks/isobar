/* palette.cpp - see palette.h */
#include "palette.h"

/* n-segment ramp: c[0..n] are the anchor colors; the gray value maps
 * linearly onto 0..n, so anchor k sits at v = 255*k/n (segment width
 * 255/n: ~64 for mode 1, 85 for mode 2, ~42.5 for mode 3). */
struct Ramp {
    int n;                 /* number of segments        */
    uint8_t c[8][3];       /* n+1 anchor colors (R,G,B) */
};

static const Ramp RAMPS[] = {
    /* mode 0: monotone - black -> white */
    { 1, { { 0, 0, 0 },       { 255, 255, 255 } } },
    /* mode 1: blue -> cyan -> green -> yellow -> red */
    { 4, { { 0, 0, 255 },     { 0, 255, 255 },   { 0, 255, 0 },
           { 255, 255, 0 },   { 255, 0, 0 } } },
    /* mode 2: black -> blue -> cyan -> white */
    { 3, { { 0, 0, 0 },       { 0, 0, 255 },     { 0, 255, 255 },
           { 255, 255, 255 } } },
    /* mode 3: black -> blue -> cyan -> green -> yellow -> red -> white */
    { 6, { { 0, 0, 0 },       { 0, 0, 255 },     { 0, 255, 255 },
           { 0, 255, 0 },     { 255, 255, 0 },   { 255, 0, 0 },
           { 255, 255, 255 } } },
};

void palette_build(int mode, int invert, uint8_t out[256][3])
{
    if (mode < 0 || mode > 3)
        mode = 0;
    const Ramp *r = &RAMPS[mode];
    for (int i = 0; i < 256; i++) {
        int v = invert ? 255 - i : i;
        double pos = (double)v * r->n / 255.0;
        int seg = (int)pos;
        if (seg >= r->n)
            seg = r->n - 1;
        double f = pos - seg;   /* v=255 -> f=1.0: exact final anchor */
        for (int k = 0; k < 3; k++) {
            double a = r->c[seg][k], b = r->c[seg + 1][k];
            out[i][k] = (uint8_t)(a + (b - a) * f + 0.5);
        }
    }
}
