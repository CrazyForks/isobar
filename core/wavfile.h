/* wavfile.h - minimal RIFF/WAVE reader.
 *
 * Accepts: 16-bit PCM, any channel count (downmixed to mono by averaging),
 * 44100 or 22050 Hz. 44100 Hz input is decimated 2:1 to 22050 Hz with a
 * windowed-sinc anti-alias filter. Anything else is rejected with an error.
 */
#ifndef ISOBAR_WAVFILE_H
#define ISOBAR_WAVFILE_H

#include <string>
#include <vector>

/* Reads `path`, returns mono samples in int16 range at 22050 Hz.
 * Throws std::runtime_error with a clear message on any problem. */
std::vector<double> wav_read_22050(const std::string &path,
                                   std::string *info_out /* may be null */);

#endif
