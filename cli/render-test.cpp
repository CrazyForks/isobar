/* render-test - the Save-image / Print / auto-save render path
 * (core/render.cpp). Registered with ctest; run via
 * `ctest --test-dir build -R render-test`.
 *
 * (audit 2026-08-09) This path had no automated cover at all, and it is
 * not theoretical: printing a partial reception once came out ~2x
 * stretched on real paper because the page height was scaled by the
 * number of lines received instead of by the original's fixed 2280-line
 * ruler (user test 2026-08-01). --test-print and --test-export exercise
 * it, but they are dev aids that need a display and a human looking at
 * the result.
 *
 * Synthetic FaxImage rather than a recording: what is being checked is
 * arithmetic - box means, palette lookup, rotation direction, the print
 * ruler - and a known ramp makes every expected value exact.
 *
 * Covers:
 *   - box-average sizes and means at scales 1 / 2 / 3, incl. the partial
 *     last box when the line count is not a multiple of the scale;
 *   - scale 1 is the identity, which is what the print path relies on;
 *   - palette mapping (monotone: gray in, gray out);
 *   - grayscale render + invert;
 *   - rotate90ccw is COUNTER-clockwise and swaps w/h;
 *   - the print ruler, including the stretch bug it exists to prevent.
 */
#include "../core/render.h"
#include "../core/palette.h"

#include <cstdio>
#include <vector>

static int fail(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

/* a value that is unique per (line, column) within a byte, so a rotation
 * or an off-by-one box cannot accidentally match */
static uint8_t val(int y, int x) { return (uint8_t)((y * 7 + x * 3) & 0xff); }

static FaxImage make_image(int lines)
{
    FaxImage img;
    for (int y = 0; y < lines; y++) {
        std::vector<uint8_t> line(FaxImage::WIDTH);
        for (int x = 0; x < FaxImage::WIDTH; x++)
            line[x] = val(y, x);
        img.lines.push_back(line);
    }
    return img;
}

/* the mean the render should produce for output pixel (x, y) at `scale` */
static int want_box(int lines, int x, int y, int scale)
{
    int sum = 0, n = 0;
    for (int dy = 0; dy < scale; dy++)
        for (int dx = 0; dx < scale; dx++) {
            int sy = y * scale + dy, sx = x * scale + dx;
            if (sy < lines && sx < FaxImage::WIDTH) {
                sum += val(sy, sx);
                n++;
            }
        }
    return n ? sum / n : 0;
}

int main()
{
    uint8_t mono[256][3], color[256][3];
    palette_build(0, 0, mono);    /* monotone, not inverted */
    palette_build(3, 0, color);

    /* 100 lines: divisible by 2, NOT by 3, so scale 3 exercises the
     * partial last box (34 rows out of 100 lines = 33*3 + 1) */
    const int LINES = 100;
    FaxImage img = make_image(LINES);

    /* ---- RGB box average at each kind's scale ---- */
    for (int scale = 1; scale <= 3; scale++) {
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        render_export_rgb(img, scale, mono, rgb, &w, &h);

        if (w != FaxImage::WIDTH / scale)
            return fail("export width");
        if (h != (LINES + scale - 1) / scale)
            return fail("export height (partial last box)");
        if (rgb.size() != (size_t)w * h * 3)
            return fail("export buffer size");

        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x += 37) {
                int want = want_box(LINES, x, y, scale);
                const uint8_t *px = &rgb[((size_t)y * w + x) * 3];
                /* monotone palette is gray in / gray out */
                if (px[0] != want || px[1] != want || px[2] != want)
                    return fail("export box mean through the palette");
            }
    }

    /* ---- scale 1 is the plain raster: what the print path depends on ---- */
    {
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        render_export_rgb(img, 1, color, rgb, &w, &h);
        if (w != FaxImage::WIDTH || h != LINES)
            return fail("scale 1 is not the full raster");
        for (int y = 0; y < h; y += 11)
            for (int x = 0; x < w; x += 53) {
                const uint8_t *c = color[val(y, x)];
                const uint8_t *px = &rgb[((size_t)y * w + x) * 3];
                if (px[0] != c[0] || px[1] != c[1] || px[2] != c[2])
                    return fail("scale 1 palette lookup");
            }
    }

    /* ---- grayscale render + invert (the auto-save PNG) ---- */
    for (int inv = 0; inv <= 1; inv++) {
        std::vector<uint8_t> g;
        int w = 0, h = 0;
        render_export_gray(img, 2, inv, g, &w, &h);
        if (w != FaxImage::WIDTH / 2 || h != LINES / 2)
            return fail("gray render size");
        if (g.size() != (size_t)w * h)
            return fail("gray buffer size (must be 1 byte/px)");
        for (int y = 0; y < h; y += 7)
            for (int x = 0; x < w; x += 41) {
                int want = want_box(LINES, x, y, 2);
                if (inv)
                    want = 255 - want;
                if (g[(size_t)y * w + x] != (uint8_t)want)
                    return fail(inv ? "gray invert" : "gray box mean");
            }
    }

    /* ---- rotation direction: CCW, not CW ----
     * Under a counter-clockwise turn the source's top-RIGHT corner
     * becomes the destination's top-left. (Clockwise would put the
     * top-left there, and that is the way round that printed a JMH
     * chart upside down.) */
    {
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        render_export_rgb(img, 1, mono, rgb, &w, &h);
        int sw = w, sh = h;
        render_rotate90ccw(rgb, &w, &h);
        if (w != sh || h != sw)
            return fail("rotate did not swap w/h");
        uint8_t top_right = val(0, sw - 1);
        if (rgb[0] != top_right)
            return fail("rotate is not counter-clockwise");
        /* and the source's top-left ends at the destination's bottom-left */
        uint8_t top_left = val(0, 0);
        if (rgb[((size_t)(h - 1) * w + 0) * 3] != top_left)
            return fail("rotate corner mapping");
    }

    /* ---- the print ruler ----
     * A partial reception must occupy lines/2280 of the page, NOT the
     * whole page. This is the assertion that would have caught the ~2x
     * stretched print: 1157 lines of a 2280-line drum on a 792-unit page
     * is 402 units, and stretching to fill would have given 792. */
    {
        if (render_print_height(792, FaxImage::MAX_LINES) != 792)
            return fail("a full 2280-line image should fill the page");
        int th = render_print_height(792, 1157);
        if (th != 402)
            return fail("partial reception is not scaled by the 2280 ruler");
        if (th >= 792)
            return fail("partial reception stretched to the full page");
        /* a receive buffer past 2280 lines clamps, it does not overflow */
        if (render_print_height(792, FaxImage::HARD_MAX_LINES) != 792)
            return fail("oversized buffer did not clamp to the page");
        if (render_print_height(792, 1) < 1)
            return fail("a single line must still be at least 1 unit tall");
    }

    printf("render-test: PASS\n");
    return 0;
}
