/* wavfile.cpp - see wavfile.h. */

#include "wavfile.h"
#include "filters.h"
#include "resample.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <cstdio>

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

std::vector<double> wav_read_22050(const std::string &path, std::string *info_out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open '" + path + "'");

    /* RIFF header */
    uint8_t hdr[12];
    if (!f.read((char *)hdr, 12) || memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0)
        throw std::runtime_error("'" + path + "' is not a RIFF/WAVE file");

    /* walk chunks to find fmt and data */
    bool have_fmt = false, have_data = false;
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> raw;      /* interleaved 16-bit samples */
    uint16_t block_align = 0;
    uint16_t tag = 0;              /* the fmt tag as written, for messages */

    for (;;) {
        uint8_t ch[8];
        if (!f.read((char *)ch, 8))
            break;
        uint32_t size = rd_u32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            std::vector<uint8_t> buf(size);
            if (!f.read((char *)buf.data(), size))
                throw std::runtime_error("truncated fmt chunk");
            if (size < 16)
                throw std::runtime_error("bad fmt chunk");
            format = tag = rd_u16(buf.data() + 0);
            channels = rd_u16(buf.data() + 2);
            rate = rd_u32(buf.data() + 4);
            block_align = rd_u16(buf.data() + 12);
            bits = rd_u16(buf.data() + 14);

            /* WAVE_FORMAT_EXTENSIBLE (0xFFFE): the real format tag is the
             * first two bytes of a 16-byte SubFormat GUID, whose remaining
             * 14 bytes are the fixed KSDATAFORMAT_SUBTYPE suffix. macOS
             * `afconvert -f WAVE` writes this whenever the source carries a
             * channel layout (any .m4a, for one), so a perfectly ordinary
             * 16-bit PCM file arrives tagged 0xFFFE. Layout after the 16
             * common bytes: cbSize(2) validBits(2) channelMask(4) GUID(16). */
            if (format == 0xFFFE) {
                static const uint8_t ks_suffix[14] = {
                    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                    0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
                };
                if (size < 40 || rd_u16(buf.data() + 16) < 22)
                    throw std::runtime_error("WAVE_FORMAT_EXTENSIBLE fmt chunk "
                                             "is too short to hold a SubFormat "
                                             "GUID");
                if (memcmp(buf.data() + 26, ks_suffix, 14) != 0)
                    throw std::runtime_error("unrecognised SubFormat GUID in "
                                             "WAVE_FORMAT_EXTENSIBLE fmt chunk");
                format = rd_u16(buf.data() + 24);
            }
            have_fmt = true;
        } else if (memcmp(ch, "data", 4) == 0) {
            raw.resize(size);
            if (!f.read((char *)raw.data(), size))
                throw std::runtime_error("truncated data chunk");
            have_data = true;
        } else {
            f.seekg(size, std::ios::cur);
        }
        if (size & 1)               /* chunks are word-aligned */
            f.seekg(1, std::ios::cur);
    }

    if (!have_fmt || !have_data)
        throw std::runtime_error("missing fmt or data chunk in '" + path + "'");
    if (format != 1) {
        std::string what = "only uncompressed PCM WAV is supported (this file "
                           "is format " + std::to_string(format);
        if (tag == 0xFFFE)
            what += ", inside WAVE_FORMAT_EXTENSIBLE";
        what += format == 3 ? ", IEEE float; re-convert to 16-bit PCM, e.g. "
                              "`afconvert -f WAVE -d LEI16 in.wav out.wav`)"
                            : ")";
        throw std::runtime_error(what);
    }
    if (bits != 16)
        throw std::runtime_error("only 16-bit PCM WAV is supported");
    if (channels < 1 || block_align != channels * 2)
        throw std::runtime_error("bad channel count / block alignment");
    /* Any sample rate is accepted (SDR recorders emit 8k/11.025k/12k/48k
     * as readily as 44.1k); it is resampled to the decoder's 22050 Hz
     * below. The only hard floor is Nyquist above the 2300 Hz white
     * subcarrier, with a little room for the band-pass skirt. */
    if (rate < 6000)
        throw std::runtime_error("sample rate " + std::to_string(rate) +
                                 " Hz is too low (need at least 6000 Hz: "
                                 "the fax signal reaches 2300 Hz)");

    /* de-interleave + downmix to mono */
    size_t frames = raw.size() / block_align;
    std::vector<double> mono(frames);
    for (size_t i = 0; i < frames; i++) {
        long acc = 0;
        for (int c = 0; c < channels; c++) {
            const uint8_t *p = &raw[(i * channels + c) * 2];
            int16_t s = (int16_t)(p[0] | (p[1] << 8));
            acc += s;
        }
        mono[i] = (double)acc / channels;
    }

    const char *how = "";
    if (rate == 44100) {
        /* 2:1 decimation: anti-alias lowpass, then drop every 2nd sample.
         * Signal of interest is <= 2300 Hz; a 63-tap FIR with 5 kHz
         * cutoff gives plenty of stopband rejection above 11025 Hz.
         * Kept as its own exact path (rather than folding into the
         * general resampler below) because every decode this project has
         * been validated against came through it. */
        Fir aa;
        aa.init(design_lowpass(63, 5000.0, 44100.0));
        std::vector<double> out;
        out.reserve(frames / 2 + 1);
        for (size_t i = 0; i < frames; i++) {
            double y = aa.step(mono[i]);
            if ((i & 1) == 1)
                out.push_back(y);
        }
        mono.swap(out);
        how = " (decimated 2:1 to 22050 Hz)";
    } else if (rate != 22050) {
        /* Any other rate: the same streaming resampler the live-audio
         * path uses (windowed-sinc lowpass at the lower of the two
         * Nyquists, then linear-interpolated pick-off). Works in both
         * directions, so 12000 Hz up and 48000 Hz down are one path. */
        Resampler rs;
        rs.init((double)rate, 22050.0);
        std::vector<double> out;
        out.reserve((size_t)(frames * (22050.0 / rate)) + 64);
        for (size_t i = 0; i < frames; i++)
            rs.feed(mono[i], out);
        mono.swap(out);
        how = " (resampled to 22050 Hz)";
    }

    if (info_out) {
        char buf[160];
        snprintf(buf, sizeof buf,
                 "%u Hz, %u ch, 16-bit PCM, %.1f s%s",
                 rate, channels, (double)frames / rate, how);
        *info_out = buf;
    }
    return mono;
}
