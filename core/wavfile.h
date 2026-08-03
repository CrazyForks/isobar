/* wavfile.h - minimal RIFF/WAVE reader.
 *
 * Accepts: 16-bit PCM, any channel count (downmixed to mono by averaging),
 * any sample rate from 6000 Hz up. PCM tagged WAVE_FORMAT_EXTENSIBLE
 * (0xFFFE) is read too — macOS `afconvert -f WAVE` writes that whenever the
 * source has a channel layout. 22050 Hz is used as-is; 44100 Hz is
 * decimated 2:1 with a windowed-sinc anti-alias filter; every other rate
 * goes through core/resample.h (up or down). The 6000 Hz floor is
 * Nyquist for the 2300 Hz white subcarrier plus filter skirt.
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
