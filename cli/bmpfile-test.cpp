/* bmpfile-test - round-trip checks for core/bmpfile.cpp.
 * Registered with ctest; run via `ctest --test-dir build -R bmpfile-test`.
 *
 * Covers:
 *   - 24-bit write/read round-trip at an odd width (row padding);
 *   - the reader's top-down variant (negative height);
 *   - the reader's 32-bit variant (alpha byte dropped);
 *   - refusal of a non-BMP file.
 */
#include "../core/bmpfile.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

static int fail(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

static uint8_t pix(int y, int x, int c) { return (uint8_t)(y * 11 + x * 5 + c); }

static std::vector<uint8_t> make_rgb(int w, int h)
{
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++)
                rgb[((size_t)y * w + x) * 3 + c] = pix(y, x, c);
    return rgb;
}

static int check_roundtrip(const std::string &path,
                           const std::vector<uint8_t> &rgb, int w, int h)
{
    std::vector<uint8_t> back;
    int rw = 0, rh = 0;
    try {
        bmp_read(path, back, &rw, &rh);
    } catch (const std::exception &e) {
        printf("FAIL: read %s: %s\n", path.c_str(), e.what());
        return 1;
    }
    if (rw != w || rh != h)
        return fail("round-trip dimensions");
    if (back != rgb)
        return fail("round-trip pixel data");
    return 0;
}

int main()
{
    namespace fs = std::filesystem;
    std::string tmpdir = fs::temp_directory_path().string();

    /* ---- 24-bit round-trip, odd width exercises the 4-byte padding ---- */
    const int W = 13, H = 7;
    std::vector<uint8_t> rgb = make_rgb(W, H);
    std::string path = tmpdir + "/isobar-bmpfile-test.bmp";
    try {
        bmp_write(path, rgb.data(), W, H);
    } catch (const std::exception &e) {
        printf("FAIL: write: %s\n", e.what());
        return 1;
    }
    if (check_roundtrip(path, rgb, W, H))
        return 1;

    /* ---- same file with a negative height must read identically ---- */
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in)
            return fail("reopen written file");
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        in.close();
        long h = (long)H;
        unsigned long neg = (unsigned long)(-h);
        for (int i = 0; i < 4; i++)
            file[22 + i] = (uint8_t)(neg >> (8 * i));
        /* pixel rows are stored bottom-up in the file; a top-down header
         * means the reader must take them in stored order, so flip the
         * rows on disk to keep the expected image the same */
        int stride = (W * 3 + 3) & ~3;
        std::vector<uint8_t> body(file.begin() + 54, file.end());
        for (int y = 0; y < H / 2; y++)
            for (int x = 0; x < stride; x++)
                std::swap(body[(size_t)y * stride + x],
                          body[(size_t)(H - 1 - y) * stride + x]);
        memcpy(file.data() + 54, body.data(), body.size());
        std::string tpath = tmpdir + "/isobar-bmpfile-test-topdown.bmp";
        {
            std::ofstream f(tpath.c_str(), std::ios::binary);
            f.write((const char *)file.data(), file.size());
        }
        if (check_roundtrip(tpath, rgb, W, H))
            return 1;
        fs::remove(tpath);
    }
    fs::remove(path);

    /* ---- 32-bit BMP: alpha byte present, must be dropped ---- */
    {
        const int W2 = 5, H2 = 3;
        std::vector<uint8_t> want = make_rgb(W2, H2);
        std::string p32 = tmpdir + "/isobar-bmpfile-test-32.bmp";
        {
            std::ofstream f(p32.c_str(), std::ios::binary);
            auto put16 = [&](unsigned v) {
                f.put((char)(v & 0xff)); f.put((char)((v >> 8) & 0xff));
            };
            auto put32 = [&](unsigned long v) {
                for (int i = 0; i < 4; i++) f.put((char)(v >> (8 * i)));
            };
            unsigned long img = (unsigned long)W2 * H2 * 4;
            f.write("BM", 2);
            put32(54 + img); put32(0); put32(54);
            put32(40); put32(W2); put32(H2);
            put16(1); put16(32); put32(0); put32(img);
            put32(2835); put32(2835); put32(0); put32(0);
            for (int y = H2 - 1; y >= 0; y--)   /* bottom-up */
                for (int x = 0; x < W2; x++) {
                    const uint8_t *p = &want[((size_t)y * W2 + x) * 3];
                    f.put((char)p[2]); f.put((char)p[1]);
                    f.put((char)p[0]); f.put((char)0xff);
                }
        }
        if (check_roundtrip(p32, want, W2, H2))
            return 1;
        fs::remove(p32);
    }

    /* ---- non-BMP input must throw ---- */
    {
        std::string bpath = tmpdir + "/isobar-bmpfile-test-junk.bmp";
        {
            std::ofstream f(bpath.c_str(), std::ios::binary);
            f.write("not a bitmap file at all.........", 32);
        }
        std::vector<uint8_t> back;
        int w, h;
        try {
            bmp_read(bpath, back, &w, &h);
            return fail("non-BMP file not refused");
        } catch (const std::exception &) {
        }
        fs::remove(bpath);
    }

    printf("bmpfile-test: PASS\n");
    return 0;
}
