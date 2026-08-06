/* synfile.h - read/write KG-FAX compatible .syn files.
 *
 * Format (docs/01-program-analysis.md section 7):
 *   offset  size  content
 *   0       7     "SynFax2"  (legacy read: "Syn Fax")
 *   7       1     mode byte (rpm/color mode; coerced 1 -> 3 on load)
 *   8       1     negative flag
 *   9       2     line count: byte0 = n/255, byte1 = n%255 (radix-255,
 *                 high byte first — NOT standard little/big-endian)
 *   11      N*1500  raw image lines, 1 byte/px brightness
 *
 * Width is not stored; it is fixed 1500 px. The legacy "Syn Fax" variant
 * has no line-count field either: its body is always 2280 lines x
 * 2000 bytes, decimated to 1500 px/line on load (out[(i*3)>>2] = in[i]).
 *
 * Written for interoperability only; this is a fresh implementation of a
 * documented file layout, not derived from the original program's code.
 */
#ifndef ISOBAR_SYNFILE_H
#define ISOBAR_SYNFILE_H

#include "syncscan.h"

#include <string>

struct SynHeader {
    uint8_t mode;       /* rpm/color mode byte (mode 1 coerced to 3 on load) */
    uint8_t negative;   /* nonzero = negative image flag */
    int     line_count;
};

/* Write img as a .syn file. mode/negative default to 0 (120 rpm, positive).
 * Throws std::runtime_error on I/O failure or too many lines. */
void syn_write(const std::string &path, const FaxImage &img,
               uint8_t mode = 0, uint8_t negative = 0);

/* Read a .syn file. Accepts "SynFax2" and legacy "Syn Fax" magic.
 * Throws std::runtime_error on bad magic, truncation, or I/O failure. */
FaxImage syn_read(const std::string &path, SynHeader *hdr);

#endif
