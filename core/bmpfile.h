/* bmpfile.h - minimal 24-bit BMP writer (dependency-free).
 *
 * The original exports BMP/JPG via its Form3 dialog (docs/01 sec. 4);
 * BMP is the always-available half of that pair. Row order on disk is
 * bottom-up (the BMP convention), so callers pass rows top-down.
 */
#ifndef ISOBAR_BMPFILE_H
#define ISOBAR_BMPFILE_H

#include <cstdint>
#include <string>

/* Write w*h packed RGB (3 bytes/pixel, top row first) as a 24-bit BMP.
 * Throws std::runtime_error on failure. */
void bmp_write(const std::string &path, const uint8_t *rgb, int w, int h);

#endif
