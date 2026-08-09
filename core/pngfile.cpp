/* pngfile.cpp - see pngfile.h */
#include "pngfile.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string.h>   /* memcmp */

#include <zlib.h>

namespace {

const uint8_t PNG_SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

/* ---- writing ---- */

void put32be(std::ostream &f, unsigned long v)
{
    f.put((char)((v >> 24) & 0xff));
    f.put((char)((v >> 16) & 0xff));
    f.put((char)((v >> 8) & 0xff));
    f.put((char)(v & 0xff));
}

/* one PNG chunk: length, type, data, crc32(type + data) */
void write_chunk(std::ostream &f, const char type[4],
                 const uint8_t *data, unsigned long len)
{
    put32be(f, len);
    f.write(type, 4);
    if (len && !f.write((const char *)data, len))
        throw std::runtime_error("png_write: write failed");
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)type, 4);
    if (len)
        crc = crc32(crc, data, len);
    put32be(f, crc);
}

/* common write path: px is w*h*bpp, ctype 0 (gray) or 2 (RGB) */
void png_write(const std::string &path, const uint8_t *px, int w, int h,
               int bpp, int ctype)
{
    if (w <= 0 || h <= 0)
        throw std::runtime_error("png_write: empty image");

    /* raw scanlines, each prefixed with filter byte 0 (None) */
    unsigned long raw_len = (unsigned long)h * (1 + (unsigned long)w * bpp);
    std::vector<uint8_t> raw(raw_len);
    for (int y = 0; y < h; y++) {
        uint8_t *dst = &raw[(size_t)y * (1 + (size_t)w * bpp)];
        dst[0] = 0;
        memcpy(dst + 1, px + (size_t)y * w * bpp, (size_t)w * bpp);
    }

    uLongf zlen = compressBound(raw_len);
    std::vector<uint8_t> z(zlen);
    /* level 9: these files are written for archiving (auto-save), where
     * size matters more than the fraction of a second the encode costs */
    if (compress2(z.data(), &zlen, raw.data(), raw_len, 9) != Z_OK)
        throw std::runtime_error("png_write: zlib compression failed");

    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f)
        throw std::runtime_error("png_write: cannot create " + path);
    f.write((const char *)PNG_SIG, 8);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16);
    ihdr[2] = (uint8_t)(w >> 8);  ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6] = (uint8_t)(h >> 8);  ihdr[7] = (uint8_t)h;
    ihdr[8] = 8;                  /* bit depth       */
    ihdr[9] = (uint8_t)ctype;     /* color type      */
    ihdr[10] = 0;                 /* compression     */
    ihdr[11] = 0;                 /* filter          */
    ihdr[12] = 0;                 /* no interlace    */
    write_chunk(f, "IHDR", ihdr, 13);
    write_chunk(f, "IDAT", z.data(), zlen);
    write_chunk(f, "IEND", 0, 0);
    if (!f)
        throw std::runtime_error("png_write: write failed");
}

/* ---- reading ---- */

unsigned long get32be(const uint8_t *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | p[3];
}

int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* reverse the per-scanline filters (PNG spec 6.2) in place */
void unfilter(std::vector<uint8_t> &raw, int w, int h, int bpp)
{
    size_t stride = (size_t)w * bpp;
    for (int y = 0; y < h; y++) {
        uint8_t *line = &raw[(size_t)y * (stride + 1)];
        uint8_t *cur = line + 1;
        const uint8_t *up = y ? cur - (stride + 1) : 0;
        uint8_t filter = line[0];
        if (filter > 4)
            throw std::runtime_error("png_read: bad filter type");
        for (size_t x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? cur[x - bpp] : 0;
            int b = up ? up[x] : 0;
            int c = (up && x >= (size_t)bpp) ? up[x - bpp] : 0;
            switch (filter) {
            case 0: break;
            case 1: cur[x] = (uint8_t)(cur[x] + a); break;
            case 2: cur[x] = (uint8_t)(cur[x] + b); break;
            case 3: cur[x] = (uint8_t)(cur[x] + (a + b) / 2); break;
            case 4: cur[x] = (uint8_t)(cur[x] + paeth(a, b, c)); break;
            }
        }
    }
}

} /* namespace */

void png_write_gray(const std::string &path, const uint8_t *gray, int w, int h)
{
    png_write(path, gray, w, h, 1, 0);
}

void png_write_rgb(const std::string &path, const uint8_t *rgb, int w, int h)
{
    png_write(path, rgb, w, h, 3, 2);
}

void png_read_gray(const std::string &path, std::vector<uint8_t> &gray,
                   int *ow, int *oh)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f)
        throw std::runtime_error("png_read: cannot open " + path);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 8 || memcmp(file.data(), PNG_SIG, 8) != 0)
        throw std::runtime_error("png_read: not a PNG file: " + path);

    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    bool have_ihdr = false;
    std::vector<uint8_t> idat;
    size_t pos = 8;
    while (pos + 12 <= file.size()) {
        unsigned long len = get32be(&file[pos]);
        const uint8_t *type = &file[pos + 4];
        if (pos + 12 + len > file.size())
            throw std::runtime_error("png_read: truncated file");
        const uint8_t *data = &file[pos + 8];
        if (!have_ihdr) {
            if (memcmp(type, "IHDR", 4) != 0 || len != 13)
                throw std::runtime_error("png_read: bad IHDR");
            w = (int)get32be(data);
            h = (int)get32be(data + 4);
            depth = data[8];
            ctype = data[9];
            interlace = data[12];
            have_ihdr = true;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + len);
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }
    if (!have_ihdr || idat.empty())
        throw std::runtime_error("png_read: no image data");
    if (w <= 0 || h <= 0)
        throw std::runtime_error("png_read: empty image");
    if (depth != 8 || interlace != 0 ||
        (ctype != 0 && ctype != 2 && ctype != 6))
        throw std::runtime_error(
            "png_read: only 8-bit non-interlaced grayscale/RGB PNG "
            "is supported: " + path);

    int bpp = ctype == 0 ? 1 : (ctype == 2 ? 3 : 4);
    unsigned long raw_len = (unsigned long)h * (1 + (unsigned long)w * bpp);
    std::vector<uint8_t> raw(raw_len);
    uLongf have = raw_len;
    if (uncompress(raw.data(), &have, idat.data(), (uLong)idat.size()) != Z_OK
        || have != raw_len)
        throw std::runtime_error("png_read: corrupt image data");
    unfilter(raw, w, h, bpp);

    gray.assign((size_t)w * h, 0);
    size_t stride = (size_t)w * bpp;
    for (int y = 0; y < h; y++) {
        const uint8_t *src = &raw[(size_t)y * (stride + 1) + 1];
        uint8_t *dst = &gray[(size_t)y * w];
        if (bpp == 1) {
            memcpy(dst, src, (size_t)w);
        } else {
            for (int x = 0; x < w; x++) {
                const uint8_t *p = src + (size_t)x * bpp;
                /* ITU-R 601 luma, approximated in fixed point */
                dst[x] = (uint8_t)((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8);
            }
        }
    }
    *ow = w;
    *oh = h;
}
