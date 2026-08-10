/* render.cpp - see render.h */
#include "render.h"

#include <cstddef>

namespace {

/* Mean of the scale x scale source box whose top-left is (x*scale,
 * y*scale), clipped at the image's edges. The one piece of arithmetic
 * both renders share; `n` cannot be 0 for any box the callers ask for
 * (they stop at the last box that starts inside the image), but the
 * guard keeps that a local fact rather than a caller's promise. */
int box_mean(const FaxImage &img, int x, int y, int scale)
{
    const int sw = FaxImage::WIDTH;
    const int sh = (int)img.lines.size();
    int sum = 0, n = 0;
    for (int dy = 0; dy < scale; dy++)
        for (int dx = 0; dx < scale; dx++) {
            int sy = y * scale + dy, sx = x * scale + dx;
            if (sy < sh && sx < sw) {
                sum += img.lines[(size_t)sy][(size_t)sx];
                n++;
            }
        }
    return n ? sum / n : 0;
}

/* Output size for a box-average at `scale`: the width divides exactly
 * (1500 / 1, 2, 3), the height rounds up so a partial last box still
 * prints its lines. */
void out_size(const FaxImage &img, int scale, int *w, int *h)
{
    *w = FaxImage::WIDTH / scale;
    *h = ((int)img.lines.size() + scale - 1) / scale;
}

} /* namespace */

void render_export_rgb(const FaxImage &img, int scale,
                       const uint8_t pal[256][3],
                       std::vector<uint8_t> &out, int *ow, int *oh)
{
    if (scale < 1)
        scale = 1;
    int w, h;
    out_size(img, scale, &w, &h);
    out.assign((size_t)w * h * 3, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *c = pal[box_mean(img, x, y, scale)];
            uint8_t *px = &out[((size_t)y * w + x) * 3];
            px[0] = c[0];
            px[1] = c[1];
            px[2] = c[2];
        }
    *ow = w;
    *oh = h;
}

void render_export_gray(const FaxImage &img, int scale, int invert,
                        std::vector<uint8_t> &out, int *ow, int *oh)
{
    if (scale < 1)
        scale = 1;
    int w, h;
    out_size(img, scale, &w, &h);
    out.assign((size_t)w * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t v = (uint8_t)box_mean(img, x, y, scale);
            out[(size_t)y * w + x] = invert ? (uint8_t)(255 - v) : v;
        }
    *ow = w;
    *oh = h;
}

void render_rotate90ccw(std::vector<uint8_t> &rgb, int *w, int *h)
{
    int sw = *w, sh = *h;
    std::vector<uint8_t> out(rgb.size());
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            int nx = y, ny = sw - 1 - x;
            uint8_t *d = &out[((size_t)ny * sh + nx) * 3];
            const uint8_t *s = &rgb[((size_t)y * sw + x) * 3];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    rgb.swap(out);
    *w = sh;
    *h = sw;
}

int render_print_height(int page_h, int lines)
{
    int th = (int)((double)page_h * lines / FaxImage::MAX_LINES + 0.5);
    if (th < 1)
        th = 1;
    if (th > page_h)
        th = page_h;   /* buffer past 2280 lines (.bmp autosave) */
    return th;
}
