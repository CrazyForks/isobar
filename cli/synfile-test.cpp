/* synfile-test - round-trip and legacy-format checks for core/synfile.cpp.
 * Registered with ctest; run via `ctest --test-dir build -R synfile-test`.
 *
 * Covers the interop contract that had no direct coverage (audit 2026-08-06
 * sec. 3.5.1):
 *   - write/read round-trip at the radix-255 line-count boundaries
 *     (254/255/256 and the 2280 maximum), including the encoded bytes
 *     themselves;
 *   - the legacy "Syn Fax" layout: no count field, fixed 2000x2280 body,
 *     decimated out[(j*3)>>2] = in[j];
 *   - the original's color-mode 1 -> 3 coercion on load.
 */
#include "../core/synfile.h"

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

/* deterministic line content: line i, pixel j */
static uint8_t pix(int i, int j) { return (uint8_t)(i * 7 + j * 3); }

static FaxImage make_image(int n)
{
    FaxImage img;
    for (int i = 0; i < n; i++) {
        std::vector<uint8_t> line(FaxImage::WIDTH);
        for (int j = 0; j < FaxImage::WIDTH; j++)
            line[j] = pix(i, j);
        img.lines.push_back(std::move(line));
    }
    return img;
}

static int check_image(const FaxImage &img, int n)
{
    if ((int)img.lines.size() != n)
        return fail("round-trip line count");
    for (int i = 0; i < n; i++)
        for (int j = 0; j < FaxImage::WIDTH; j++)
            if (img.lines[i][j] != pix(i, j))
                return fail("round-trip pixel data");
    return 0;
}

int main()
{
    namespace fs = std::filesystem;
    std::string tmpdir = fs::temp_directory_path().string();

    /* ---- SynFax2 round-trip at the radix-255 boundaries ---- */
    const int counts[] = { 254, 255, 256, FaxImage::MAX_LINES };
    for (int c = 0; c < 4; c++) {
        int n = counts[c];
        std::string path = tmpdir + "/isobar-synfile-test.syn";
        try {
            syn_write(path, make_image(n), 2, 1);
        } catch (const std::exception &e) {
            printf("FAIL: write n=%d: %s\n", n, e.what());
            return 1;
        }

        /* pin the on-disk header bytes: magic, mode, negative, then the
         * radix-255 count (hi = n/255, lo = n%255 — NOT little-endian) */
        std::ifstream f(path.c_str(), std::ios::binary);
        if (!f)
            return fail("reopen written file");
        unsigned char h[11];
        if (!f.read((char *)h, 11))
            return fail("read back header");
        if (memcmp(h, "SynFax2", 7) != 0)
            return fail("magic bytes");
        if (h[7] != 2 || h[8] != 1)
            return fail("mode/negative bytes");
        if (h[9] != (unsigned char)(n / 255) || h[10] != (unsigned char)(n % 255))
            return fail("radix-255 count encoding");

        SynHeader hdr;
        FaxImage img;
        try {
            img = syn_read(path, &hdr);
        } catch (const std::exception &e) {
            printf("FAIL: read n=%d: %s\n", n, e.what());
            return 1;
        }
        if (hdr.mode != 2 || hdr.negative != 1 || hdr.line_count != n)
            return fail("round-trip header fields");
        if (check_image(img, n))
            return 1;
        f.close();          /* Windows refuses to delete an open file */
        fs::remove(path);
    }

    /* too many lines must be refused (radix-255 field tops out at 2280) */
    try {
        syn_write(tmpdir + "/isobar-synfile-toobig.syn",
                  make_image(FaxImage::MAX_LINES + 1), 0, 0);
        return fail("over-long write not refused");
    } catch (const std::exception &) {
    }

    /* ---- mode 1 -> 3 coercion on load (both formats) ---- */
    {
        std::string path = tmpdir + "/isobar-synfile-mode1.syn";
        syn_write(path, make_image(1), 1, 0);
        SynHeader hdr;
        syn_read(path, &hdr);
        if (hdr.mode != 3)
            return fail("SynFax2 mode 1 not coerced to 3");
        fs::remove(path);
    }

    /* ---- legacy "Syn Fax": fixed 2000x2280 body, (j*3)>>2 decimation ---- */
    {
        std::string path = tmpdir + "/isobar-synfile-legacy.syn";
        {
            std::ofstream f(path.c_str(), std::ios::binary);
            if (!f)
                return fail("cannot write legacy fixture");
            f.write("Syn Fax", 7);
            f.put((char)1);          /* mode 1: must come back as 3 */
            f.put((char)1);          /* negative */
            for (int i = 0; i < FaxImage::MAX_LINES; i++)
                for (int j = 0; j < 2000; j++)
                    f.put((char)((i + j) & 0xff));
        }
        SynHeader hdr;
        FaxImage img;
        try {
            img = syn_read(path, &hdr);
        } catch (const std::exception &e) {
            printf("FAIL: legacy read: %s\n", e.what());
            return 1;
        }
        if (hdr.mode != 3)
            return fail("legacy mode 1 not coerced to 3");
        if (hdr.negative != 1 || hdr.line_count != FaxImage::MAX_LINES)
            return fail("legacy header fields");
        if ((int)img.lines.size() != FaxImage::MAX_LINES)
            return fail("legacy line count");
        /* expected line: apply the documented mapping in input order —
         * where two inputs land on one output pixel, the later wins */
        for (int i = 0; i < FaxImage::MAX_LINES; i++) {
            std::vector<uint8_t> want(FaxImage::WIDTH);
            for (int j = 0; j < 2000; j++)
                want[(j * 3) >> 2] = (uint8_t)((i + j) & 0xff);
            if (img.lines[i] != want)
                return fail("legacy decimation mapping");
        }
        fs::remove(path);

        /* truncated legacy body must throw, not silently decode garbage */
        {
            std::ofstream f(path.c_str(), std::ios::binary);
            f.write("Syn Fax", 7);
            f.put((char)0);
            f.put((char)0);
            f.put((char)0);          /* 1 byte of a 2000x2280 body */
        }
        try {
            syn_read(path, nullptr);
            return fail("truncated legacy file not refused");
        } catch (const std::exception &) {
        }
        fs::remove(path);
    }

    printf("synfile-test: PASS\n");
    return 0;
}
