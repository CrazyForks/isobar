/* pngfile-test - round-trip and refusal checks for core/pngfile.cpp.
 * Registered with ctest; run via `ctest --test-dir build -R pngfile-test`.
 *
 * Covers:
 *   - grayscale write/read round-trip (byte-exact);
 *   - RGB write/read round-trip through the luma reduction;
 *   - a fax-like image compresses below its raw size (level 9 IDAT);
 *   - refusal of a non-PNG file.
 */
#include "../core/pngfile.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

static int fail(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

static uint8_t pix(int y, int x) { return (uint8_t)(y * 5 + x * 3); }

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

    printf("pngfile-test: PASS\n");
    return 0;
}
