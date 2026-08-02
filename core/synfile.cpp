/* synfile.cpp - .syn file I/O. See synfile.h for the format layout. */
#include "synfile.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

static const char MAGIC[7] = {'S', 'y', 'n', 'F', 'a', 'x', '2'};
/* MAX_LINES is FaxImage::MAX_LINES (2280) — the .syn radix-255 count
 * field must fit and the original's buffer is exactly this tall. */

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
    for (auto &line : img.lines)
        f.write((const char *)line.data(), FaxImage::WIDTH);

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
        (memcmp(magic, MAGIC, 7) != 0 && memcmp(magic, "Syn Fax", 7) != 0))
        throw std::runtime_error("'" + path + "' is not a .syn file");

    /* istream::get() returns EOF (-1) past the end, like fgetc */
    int mode = f.get(), negative = f.get();
    int hi = f.get(), lo = f.get();
    int n = (hi == EOF || lo == EOF) ? -1 : hi * 255 + lo;
    if (mode == EOF || negative == EOF || n < 0 || n > FaxImage::MAX_LINES)
        throw std::runtime_error("truncated .syn header");

    FaxImage img;
    img.lines_locked = img.lines_corrected = img.lines_coasted = 0;
    img.relocks = 0;
    for (int i = 0; i < n; i++) {
        std::vector<uint8_t> line(FaxImage::WIDTH);
        if (!f.read((char *)line.data(), FaxImage::WIDTH))
            throw std::runtime_error("truncated .syn image data");
        img.lines.push_back(std::move(line));
    }

    if (hdr) {
        hdr->mode = (uint8_t)mode;
        hdr->negative = (uint8_t)negative;
        hdr->line_count = n;
    }
    return img;
}
