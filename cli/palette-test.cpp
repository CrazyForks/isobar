/* palette-test - pin the color-mapping contract of core/palette.cpp.
 * Registered with ctest; run via `ctest --test-dir build -R palette-test`.
 *
 * (audit 2026-08-06 sec. 3.5.2) Pins:
 *   - the anchor colors of every mode, at the gray values where the ramp
 *     hits them exactly (v = 255*k/n);
 *   - invert applied to the gray value FIRST, then the palette (a post-color
 *     negative would give different answers — see the mode-2 checks);
 *   - mode 3's forced-white endpoint at v=255;
 *   - unknown modes falling back to monotone.
 */
#include "../core/palette.h"

#include <cstdio>

static int fail(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

static int eq(const uint8_t px[3], int r, int g, int b)
{
    return px[0] == r && px[1] == g && px[2] == b;
}

int main()
{
    uint8_t lut[256][3];

    /* mode 0 monotone: gray in = gray out */
    palette_build(0, 0, lut);
    if (!eq(lut[0], 0, 0, 0) || !eq(lut[128], 128, 128, 128) ||
        !eq(lut[255], 255, 255, 255))
        return fail("mode 0 ramp");

    /* mode 1 (blue -> cyan -> green -> yellow -> red): only v=0 and v=255
     * land exactly on anchors (255/4 is not an integer) */
    palette_build(1, 0, lut);
    if (!eq(lut[0], 0, 0, 255))
        return fail("mode 1 v=0 not blue");
    if (!eq(lut[255], 255, 0, 0))
        return fail("mode 1 v=255 not red");

    /* mode 2 (black -> blue -> cyan -> white): 255/3 = 85, so v=85 and
     * v=170 hit the blue and cyan anchors exactly */
    palette_build(2, 0, lut);
    if (!eq(lut[0], 0, 0, 0))
        return fail("mode 2 v=0 not black");
    if (!eq(lut[85], 0, 0, 255))
        return fail("mode 2 v=85 not blue");
    if (!eq(lut[170], 0, 255, 255))
        return fail("mode 2 v=170 not cyan");
    if (!eq(lut[255], 255, 255, 255))
        return fail("mode 2 v=255 not white");

    /* invert FIRST: i=170 -> v=85 -> blue. A post-color negative of the
     * uninverted table would give yellow (255 - blue) here instead. */
    palette_build(2, 1, lut);
    if (!eq(lut[0], 255, 255, 255))
        return fail("mode 2 inverted i=0 not white");
    if (!eq(lut[170], 0, 0, 255))
        return fail("invert is not applied before the palette");
    if (!eq(lut[255], 0, 0, 0))
        return fail("mode 2 inverted i=255 not black");

    /* mode 3 (black -> blue -> cyan -> green -> yellow -> red -> white):
     * 255/6 = 42.5, so v=85 hits cyan and v=170 hits yellow exactly;
     * v=255 is the forced-white endpoint */
    palette_build(3, 0, lut);
    if (!eq(lut[0], 0, 0, 0))
        return fail("mode 3 v=0 not black");
    if (!eq(lut[85], 0, 255, 255))
        return fail("mode 3 v=85 not cyan");
    if (!eq(lut[170], 255, 255, 0))
        return fail("mode 3 v=170 not yellow");
    if (!eq(lut[255], 255, 255, 255))
        return fail("mode 3 v=255 not forced white");

    /* unknown modes fall back to monotone, like the original's UI */
    palette_build(7, 0, lut);
    if (!eq(lut[100], 100, 100, 100))
        return fail("unknown mode does not fall back to monotone");
    palette_build(-1, 0, lut);
    if (!eq(lut[100], 100, 100, 100))
        return fail("negative mode does not fall back to monotone");

    printf("palette-test: PASS\n");
    return 0;
}
