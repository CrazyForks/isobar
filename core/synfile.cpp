/* synfile.cpp - .syn file I/O. See synfile.h for the format layout. */
#include "synfile.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

static const char MAGIC[7] = {'S', 'y', 'n', 'F', 'a', 'x', '2'};
/* MAX_LINES is FaxImage::MAX_LINES (2280) — the .syn radix-255 count
 * field must fit and the original's buffer is exactly this tall. */

void syn_write(const std::string &path, const FaxImage &img,
               uint8_t mode, uint8_t negative)
{
    if ((int)img.lines.size() > FaxImage::MAX_LINES)
        throw std::runtime_error("too many lines for .syn (max 2280)");

    FILE *fp = fopen(path.c_str(), "wb");
    if (!fp)
        throw std::runtime_error("cannot write '" + path + "'");

    fwrite(MAGIC, 1, 7, fp);
    fputc(mode, fp);
    fputc(negative, fp);
    int n = (int)img.lines.size();
    fputc(n / 255, fp);           /* radix-255 big-endian line count */
    fputc(n % 255, fp);
    for (auto &line : img.lines)
        fwrite(line.data(), 1, FaxImage::WIDTH, fp);

    fclose(fp);
}

FaxImage syn_read(const std::string &path, SynHeader *hdr)
{
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp)
        throw std::runtime_error("cannot open '" + path + "'");

    char magic[7];
    if (fread(magic, 1, 7, fp) != 7 ||
        (memcmp(magic, MAGIC, 7) != 0 && memcmp(magic, "Syn Fax", 7) != 0)) {
        fclose(fp);
        throw std::runtime_error("'" + path + "' is not a .syn file");
    }

    int mode = fgetc(fp), negative = fgetc(fp);
    int hi = fgetc(fp), lo = fgetc(fp);
    int n = (hi == EOF || lo == EOF) ? -1 : hi * 255 + lo;
    if (mode == EOF || negative == EOF || n < 0 || n > FaxImage::MAX_LINES) {
        fclose(fp);
        throw std::runtime_error("truncated .syn header");
    }

    FaxImage img;
    img.lines_locked = img.lines_coasted = img.relocks = 0;
    for (int i = 0; i < n; i++) {
        std::vector<uint8_t> line(FaxImage::WIDTH);
        if (fread(line.data(), 1, FaxImage::WIDTH, fp) != FaxImage::WIDTH) {
            fclose(fp);
            throw std::runtime_error("truncated .syn image data");
        }
        img.lines.push_back(std::move(line));
    }
    fclose(fp);

    if (hdr) {
        hdr->mode = (uint8_t)mode;
        hdr->negative = (uint8_t)negative;
        hdr->line_count = n;
    }
    return img;
}
