/* pngfile-test - round-trip and refusal checks for core/pngfile.cpp.
 * Registered with ctest; run via `ctest --test-dir build -R pngfile-test`.
 *
 * Covers:
 *   - grayscale write/read round-trip (byte-exact);
 *   - RGB write/read round-trip through the luma reduction;
 *   - a fax-like image compresses below its raw size (level 9 IDAT);
 *   - refusal of a non-PNG file;
 *   - refusal of a declared size that overflows 32-bit size arithmetic.
 */
#include "../core/pngfile.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <zlib.h>

static int fail(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

static uint8_t pix(int y, int x) { return (uint8_t)(y * 5 + x * 3); }

/* ---- minimal PNG assembly, so the oversize case below can be built by
 * hand (the writer under test would never produce it) ---- */

static void put32be(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

static void chunk(std::vector<uint8_t> &v, const char type[4],
                  const std::vector<uint8_t> &data)
{
    put32be(v, (uint32_t)data.size());
    size_t at = v.size();
    v.insert(v.end(), type, type + 4);
    v.insert(v.end(), data.begin(), data.end());
    uLong c = crc32(0L, Z_NULL, 0);
    c = crc32(c, &v[at], (uInt)(4 + data.size()));
    put32be(v, (uint32_t)c);
}

int main()
{
    namespace fs = std::filesystem;
    std::string tmpdir = fs::temp_directory_path().string();

    /* ---- grayscale round-trip ---- */
    const int W = 64, H = 50;
    std::vector<uint8_t> gray(W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            gray[(size_t)y * W + x] = pix(y, x);

    std::string gpath = tmpdir + "/isobar-pngfile-test-gray.png";
    try {
        png_write_gray(gpath, gray.data(), W, H);
    } catch (const std::exception &e) {
        printf("FAIL: gray write: %s\n", e.what());
        return 1;
    }
    {
        std::vector<uint8_t> back;
        int w = 0, h = 0;
        try {
            png_read_gray(gpath, back, &w, &h);
        } catch (const std::exception &e) {
            printf("FAIL: gray read: %s\n", e.what());
            return 1;
        }
        if (w != W || h != H)
            return fail("gray round-trip dimensions");
        if (back != gray)
            return fail("gray round-trip pixel data");
    }
    fs::remove(gpath);

    /* ---- RGB round-trip (read reduces to luma) ---- */
    std::vector<uint8_t> rgb(W * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *p = &rgb[((size_t)y * W + x) * 3];
            p[0] = pix(y, x);
            p[1] = pix(x, y);
            p[2] = (uint8_t)(x + y);
        }
    std::string cpath = tmpdir + "/isobar-pngfile-test-rgb.png";
    try {
        png_write_rgb(cpath, rgb.data(), W, H);
    } catch (const std::exception &e) {
        printf("FAIL: rgb write: %s\n", e.what());
        return 1;
    }
    {
        std::vector<uint8_t> back;
        int w = 0, h = 0;
        try {
            png_read_gray(cpath, back, &w, &h);
        } catch (const std::exception &e) {
            printf("FAIL: rgb read: %s\n", e.what());
            return 1;
        }
        if (w != W || h != H)
            return fail("rgb round-trip dimensions");
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const uint8_t *p = &rgb[((size_t)y * W + x) * 3];
                uint8_t want =
                    (uint8_t)((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8);
                if (back[(size_t)y * W + x] != want)
                    return fail("rgb -> luma mapping");
            }
    }
    fs::remove(cpath);

    /* ---- a fax-like image must compress below its raw size ---- */
    {
        const int FW = 1500, FH = 400;
        std::vector<uint8_t> fax(FW * FH, 220);      /* paper white */
        for (int y = 50; y < 350; y++)               /* chart bands */
            for (int x = 100 + (y % 40); x < 1200; x += 97)
                fax[(size_t)y * FW + x] = 30;
        std::string fpath = tmpdir + "/isobar-pngfile-test-fax.png";
        png_write_gray(fpath, fax.data(), FW, FH);
        auto sz = fs::file_size(fpath);
        if (sz >= fax.size())
            return fail("fax-like image did not compress below raw size");
        fs::remove(fpath);
    }

    /* ---- non-PNG input must throw ---- */
    {
        std::string bpath = tmpdir + "/isobar-pngfile-test-junk.png";
        {
            std::ofstream f(bpath.c_str(), std::ios::binary);
            f.write("SynFax2 not a png at all", 24);
        }
        std::vector<uint8_t> back;
        int w, h;
        try {
            png_read_gray(bpath, back, &w, &h);
            return fail("non-PNG file not refused");
        } catch (const std::exception &) {
        }
        fs::remove(bpath);
    }

    /* ---- a declared size that overflows 32-bit arithmetic must be
     * refused before anything is allocated ----
     *
     * 100000 x 42950 grayscale: h*(1 + w*bpp) = 4,295,042,950, which is
     * exactly 75,654 once it wraps in a 32-bit `unsigned long` - MSVC's
     * width for that type, where macOS and Linux have 64. The reader used
     * to size its scanline buffer in `unsigned long`, so on Windows this
     * file allocated 75,654 bytes, accepted an IDAT that decompresses to
     * exactly that much (so the length check passed), and then unfiltered
     * 42,950 rows of 100,001 bytes through it - a 4.3 GB heap write out of
     * a ~1 KB file.
     *
     * The message is checked, not just the throw: before the fix a 64-bit
     * build also threw here, but only after trying to allocate 4.3 GB and
     * failing the decompressed-length check. Refusing on the SIZE is the
     * behaviour being pinned. */
    {
        const uint32_t BAD_W = 100000, BAD_H = 42950;
        const size_t WRAPPED = 75654;

        std::vector<uint8_t> png;
        const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        png.insert(png.end(), sig, sig + 8);

        std::vector<uint8_t> ihdr;
        put32be(ihdr, BAD_W);
        put32be(ihdr, BAD_H);
        ihdr.push_back(8);   /* bit depth  */
        ihdr.push_back(0);   /* gray       */
        ihdr.push_back(0);   /* deflate    */
        ihdr.push_back(0);   /* filter     */
        ihdr.push_back(0);   /* no interlace */
        chunk(png, "IHDR", ihdr);

        std::vector<uint8_t> plain(WRAPPED, 0);
        uLongf zlen = compressBound((uLong)plain.size());
        std::vector<uint8_t> z(zlen);
        if (compress2(z.data(), &zlen, plain.data(), (uLong)plain.size(), 9)
            != Z_OK)
            return fail("oversize case: could not build the IDAT");
        z.resize(zlen);
        chunk(png, "IDAT", z);
        chunk(png, "IEND", std::vector<uint8_t>());

        std::string opath = tmpdir + "/isobar-pngfile-test-oversize.png";
        {
            std::ofstream f(opath.c_str(), std::ios::binary);
            f.write((const char *)png.data(), (std::streamsize)png.size());
        }
        std::vector<uint8_t> back;
        int w = 0, h = 0;
        bool refused_on_size = false;
        try {
            png_read_gray(opath, back, &w, &h);
        } catch (const std::exception &e) {
            refused_on_size = strstr(e.what(), "too large") != 0;
        }
        fs::remove(opath);
        if (!refused_on_size)
            return fail("oversize declared image not refused on its size");
    }

    /* ---- a byte flipped in storage must be reported, not decoded ----
     *
     * PNG is the archival auto-save format precisely because it carries a
     * CRC-32 per chunk; the reader used to skip straight past it, so bit
     * rot in a saved chart came back as quiet garbage. The message is
     * checked, not just the throw: corrupting IDAT data would ALSO make
     * zlib fail a moment later, and "corrupt image data" would let a
     * reader with no CRC check at all pass this test. The CRC has to be
     * what catches it. */
    {
        const int CW = 40, CH = 30;
        std::vector<uint8_t> src((size_t)CW * CH);
        for (int y = 0; y < CH; y++)
            for (int x = 0; x < CW; x++)
                src[(size_t)y * CW + x] = pix(y, x);
        std::string rpath = tmpdir + "/isobar-pngfile-test-rot.png";
        png_write_gray(rpath, src.data(), CW, CH);

        std::vector<uint8_t> raw;
        {
            std::ifstream f(rpath.c_str(), std::ios::binary);
            raw.assign((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
        }
        /* find the IDAT chunk and flip one bit of its payload */
        size_t at = 0, pos = 8;
        while (pos + 12 <= raw.size()) {
            uint32_t len = ((uint32_t)raw[pos] << 24) |
                           ((uint32_t)raw[pos + 1] << 16) |
                           ((uint32_t)raw[pos + 2] << 8) | raw[pos + 3];
            if (memcmp(&raw[pos + 4], "IDAT", 4) == 0 && len > 0) {
                at = pos + 8 + len / 2;
                break;
            }
            pos += (size_t)12 + len;
        }
        if (at == 0)
            return fail("bit-rot case: no IDAT chunk found");
        raw[at] ^= 0x01;
        {
            std::ofstream f(rpath.c_str(), std::ios::binary);
            f.write((const char *)raw.data(), (std::streamsize)raw.size());
        }

        std::vector<uint8_t> back;
        int w = 0, h = 0;
        bool caught_by_crc = false;
        try {
            png_read_gray(rpath, back, &w, &h);
        } catch (const std::exception &e) {
            caught_by_crc = strstr(e.what(), "CRC") != 0;
        }
        fs::remove(rpath);
        if (!caught_by_crc)
            return fail("a flipped byte was not caught by the chunk CRC");
    }

    printf("pngfile-test: PASS\n");
    return 0;
}
