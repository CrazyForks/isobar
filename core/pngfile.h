/* pngfile.h - minimal PNG reader/writer on zlib (the core's only
 * external dependency).
 *
 * Writer: 8-bit grayscale (color type 0) or RGB (type 2), one IDAT,
 * filter 0 scanlines - the same everything-in-one-file spirit as
 * bmpfile.
 *
 * Reader: accepts 8-bit gray / RGB / RGBA, non-interlaced, and returns
 * GRAYSCALE (RGB(A) is reduced by luma) because PNG input exists to be
 * loaded into the 1-byte/px fax buffer. Palette, 16-bit and interlaced
 * files are refused with a clear message.
 *
 * Rows are top-down on both sides of the API.
 */
#ifndef ISOBAR_PNGFILE_H
#define ISOBAR_PNGFILE_H

#include <cstdint>
#include <string>
#include <vector>

/* Write w*h 1-byte/px grayscale (top row first) as a PNG.
 * Throws std::runtime_error on failure. */
void png_write_gray(const std::string &path, const uint8_t *gray, int w, int h);

/* Write w*h packed RGB (3 bytes/px, top row first) as a PNG.
 * Throws std::runtime_error on failure. */
void png_write_rgb(const std::string &path, const uint8_t *rgb, int w, int h);

/* Read a PNG as w*h 1-byte/px grayscale (top row first). Accepts 8-bit
 * gray, RGB and RGBA (color is reduced to luma; alpha is dropped).
 * Every CRITICAL chunk's CRC-32 is verified before its contents are used,
 * so a file damaged in storage is reported rather than decoded to garbage
 * (this is what makes PNG the archival auto-save format); a damaged
 * ancillary chunk is ignored, as the spec allows.
 * Throws std::runtime_error on bad signature, truncation, CRC mismatch,
 * an image too large to be plausible, unsupported bit depth/color
 * type/interlace, or I/O failure. */
void png_read_gray(const std::string &path, std::vector<uint8_t> &gray,
                   int *w, int *h);

#endif
