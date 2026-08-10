/* synfile.cpp - .syn file I/O. See synfile.h for the format layout. */
#include "synfile.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

static const char MAGIC[7] = {'S', 'y', 'n', 'F', 'a', 'x', '2'};
static const char MAGIC_LEGACY[7] = {'S', 'y', 'n', ' ', 'F', 'a', 'x'};
/* MAX_LINES is FaxImage::MAX_LINES (2280) — the .syn radix-255 count
 * field must fit and the original's buffer is exactly this tall. */

/* Legacy "Syn Fax" body: fixed 2000 bytes/line x 2280 lines, decimated
 * to 1500 px by out[(i*3)>>2] = in[i] (input-driven, 4 in -> 3 out). */
static const int LEGACY_WIDTH = 2000;

void syn_write(const std::string &path, const FaxImage &img,
               uint8_t mode, uint8_t negative)
{
    if ((int)img.lines.size() > FaxImage::MAX_LINES)
        throw std::runtime_error("too many lines for .syn (max 2280)");

    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot write '" + path + "'");

    f.write(MAGIC, 7);
    f.put((char)mode);
    f.put((char)negative);
    int n = (int)img.lines.size();
    f.put((char)(n / 255));       /* radix-255 big-endian line count */
    f.put((char)(n % 255));
    /* WIDTH bytes are written from every line, so a short one would be an
     * out-of-bounds read. Every FaxImage the decoder builds is WIDTH wide,
     * but the GUI assembles its own for the rotate buttons and this is a
     * public core entry point - check rather than trust. */
    for (auto &line : img.lines) {
        if ((int)line.size() != FaxImage::WIDTH)
            throw std::runtime_error("syn_write: line is not 1500 px wide");
        f.write((const char *)line.data(), FaxImage::WIDTH);
    }

    if (!f)
        throw std::runtime_error("cannot write '" + path + "'");
}

FaxImage syn_read(const std::string &path, SynHeader *hdr)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open '" + path + "'");

    char magic[7];
    if (!f.read(magic, 7) ||
        (memcmp(magic, MAGIC, 7) != 0 && memcmp(magic, MAGIC_LEGACY, 7) != 0))
        throw std::runtime_error("'" + path + "' is not a .syn file");
    bool legacy = memcmp(magic, MAGIC_LEGACY, 7) == 0;

    /* istream::get() returns EOF (-1) past the end, like fgetc */
    int mode = f.get(), negative = f.get();
    if (mode == EOF || negative == EOF)
        throw std::runtime_error("truncated .syn header");

    /* The original coerces color mode 1 to 3 on load, for both formats. */
    if (mode == 1)
        mode = 3;

    int n;
    FaxImage img;
    img.lines_locked = img.lines_corrected = img.lines_coasted = 0;
    img.relocks = 0;

    if (legacy) {
        /* Legacy "Syn Fax": no line-count field; body is always the full
         * 2280 lines x 2000 bytes, decimated to 1500 px per line. */
        n = FaxImage::MAX_LINES;
        std::vector<uint8_t> wide(LEGACY_WIDTH);
        for (int i = 0; i < n; i++) {
            if (!f.read((char *)wide.data(), LEGACY_WIDTH))
                throw std::runtime_error("truncated .syn image data");
            std::vector<uint8_t> line(FaxImage::WIDTH);
            for (int j = 0; j < LEGACY_WIDTH; j++)
                line[(j * 3) >> 2] = wide[j];
            img.lines.push_back(std::move(line));
        }
    } else {
        int hi = f.get(), lo = f.get();
        n = (hi == EOF || lo == EOF) ? -1 : hi * 255 + lo;
        if (n < 0 || n > FaxImage::MAX_LINES)
            throw std::runtime_error("truncated .syn header");
        for (int i = 0; i < n; i++) {
            std::vector<uint8_t> line(FaxImage::WIDTH);
            if (!f.read((char *)line.data(), FaxImage::WIDTH))
                throw std::runtime_error("truncated .syn image data");
            img.lines.push_back(std::move(line));
        }
    }

    if (hdr) {
        hdr->mode = (uint8_t)mode;
        hdr->negative = (uint8_t)negative;
        hdr->line_count = n;
    }
    return img;
}
