/* bmpfile.cpp - see bmpfile.h */
#include "bmpfile.h"

#include <fstream>
#include <iterator>
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

static unsigned get16(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

static unsigned long get32(const uint8_t *p)
{
    return p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

void bmp_read(const std::string &path, std::vector<uint8_t> &rgb,
              int *ow, int *oh)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f)
        throw std::runtime_error("bmp_read: cannot open " + path);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 54 || file[0] != 'B' || file[1] != 'M')
        throw std::runtime_error("bmp_read: not a BMP file: " + path);

    unsigned long data_off = get32(&file[10]);
    unsigned long hdr_size = get32(&file[14]);
    if (hdr_size < 40)
        throw std::runtime_error("bmp_read: unsupported header: " + path);
    /* width/height are signed 32-bit on disk; route through int32_t so a
     * negative (top-down) height sign-extends correctly on 64-bit long */
    long w = (long)(int32_t)get32(&file[18]);
    long h = (long)(int32_t)get32(&file[22]);   /* negative = top-down */
    int bits = (int)get16(&file[28]);
    unsigned long compression = get32(&file[30]);
    if (w <= 0 || h == 0)
        throw std::runtime_error("bmp_read: empty image");
    if (compression != 0 || (bits != 24 && bits != 32))
        throw std::runtime_error(
            "bmp_read: only uncompressed 24/32-bit BMP is supported: " + path);

    int top_down = h < 0;
    if (top_down) h = -h;
    int bpp = bits / 8;
    /* 64-bit throughout. `unsigned long` is 32 bits on MSVC and 64 on
     * macOS/Linux, so stride*h computed in it wraps on Windows for a large
     * enough declared size - the bounds check would then pass and the row
     * loop below, which indexes in size_t, would read past the file buffer.
     * (The oversized rgb.assign() happens to throw first today, but the
     * check is what is meant to be load-bearing here, not the allocator.
     * core/pngfile.cpp documents the same trap at PNG_MAX_RAW, where it
     * really did bite.) */
    uint64_t stride = ((uint64_t)w * bpp + 3) & ~(uint64_t)3;
    if ((uint64_t)data_off + stride * (uint64_t)h > (uint64_t)file.size())
        throw std::runtime_error("bmp_read: truncated file");

    rgb.assign((size_t)w * h * 3, 0);
    for (long y = 0; y < h; y++) {
        long sy = top_down ? y : h - 1 - y;
        const uint8_t *src =
            &file[(size_t)((uint64_t)data_off + (uint64_t)sy * stride)];
        uint8_t *dst = &rgb[(size_t)y * w * 3];
        for (long x = 0; x < w; x++) {
            dst[3 * x]     = src[bpp * x + 2];   /* R */
            dst[3 * x + 1] = src[bpp * x + 1];   /* G */
            dst[3 * x + 2] = src[bpp * x];       /* B */
        }
    }
    *ow = (int)w;
    *oh = (int)h;
}
