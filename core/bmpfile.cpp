/* bmpfile.cpp - see bmpfile.h */
#include "bmpfile.h"

#include <fstream>
#include <stdexcept>
#include <vector>

static void put16(std::ostream &f, unsigned v)
{
    f.put((char)(v & 0xff));
    f.put((char)((v >> 8) & 0xff));
}

static void put32(std::ostream &f, unsigned long v)
{
    f.put((char)(v & 0xff));
    f.put((char)((v >> 8) & 0xff));
    f.put((char)((v >> 16) & 0xff));
    f.put((char)((v >> 24) & 0xff));
}

void bmp_write(const std::string &path, const uint8_t *rgb, int w, int h)
{
    if (w <= 0 || h <= 0)
        throw std::runtime_error("bmp_write: empty image");

    /* rows are 4-byte aligned, zero-padded, stored bottom-up as BGR */
    int stride = (w * 3 + 3) & ~3;
    std::vector<uint8_t> row(stride);

    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f)
        throw std::runtime_error("bmp_write: cannot create " + path);

    unsigned long img_bytes = (unsigned long)stride * h;
    /* BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40) */
    f.write("BM", 2);
    put32(f, 54 + img_bytes);   /* file size          */
    put32(f, 0);                /* reserved           */
    put32(f, 54);               /* pixel data offset  */
    put32(f, 40);               /* info header size   */
    put32(f, (unsigned long)w);
    put32(f, (unsigned long)h); /* positive = bottom-up */
    put16(f, 1);                /* planes             */
    put16(f, 24);               /* bits per pixel     */
    put32(f, 0);                /* BI_RGB, no compression */
    put32(f, img_bytes);
    put32(f, 2835);             /* ~72 dpi, informational */
    put32(f, 2835);
    put32(f, 0);                /* palette colors     */
    put32(f, 0);

    for (int y = h - 1; y >= 0; y--) {
        const uint8_t *src = rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            row[3 * x]     = src[3 * x + 2];   /* B */
            row[3 * x + 1] = src[3 * x + 1];   /* G */
            row[3 * x + 2] = src[3 * x];       /* R */
        }
        if (!f.write((const char *)row.data(), stride))
            throw std::runtime_error("bmp_write: write failed");
    }
    if (!f)
        throw std::runtime_error("bmp_write: write failed");
}
