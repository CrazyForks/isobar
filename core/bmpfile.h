/* bmpfile.h - minimal BMP I/O (dependency-free).
 *
 * Writer: 24-bit only. Reader: uncompressed 24/32-bit.
 * The original exports BMP/JPG via its Form3 dialog (docs/01 sec. 4);
 * BMP is the always-available half of that pair. Row order on disk is
 * bottom-up (the BMP convention), so callers pass/get rows top-down.
 */
#ifndef ISOBAR_BMPFILE_H
#define ISOBAR_BMPFILE_H

#include <cstdint>
#include <string>
#include <vector>

/* Write w*h packed RGB (3 bytes/pixel, top row first) as a 24-bit BMP.
 * Throws std::runtime_error on failure. */
void bmp_write(const std::string &path, const uint8_t *rgb, int w, int h);

/* Read an uncompressed 24-bit or 32-bit BMP as w*h packed RGB
 * (3 bytes/pixel, top row first; the alpha byte of 32-bit files is
 * dropped). Both bottom-up and top-down row orders are handled.
 * Throws std::runtime_error on bad magic, other depths, compression,
 * truncation, or I/O failure. */
void bmp_read(const std::string &path, std::vector<uint8_t> &rgb,
              int *w, int *h);

#endif
