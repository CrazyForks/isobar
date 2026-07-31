/* wavfile.cpp - see wavfile.h. */

#include "wavfile.h"
#include "filters.h"
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
            format = rd_u16(buf.data() + 0);
            channels = rd_u16(buf.data() + 2);
            rate = rd_u32(buf.data() + 4);
            block_align = rd_u16(buf.data() + 12);
            bits = rd_u16(buf.data() + 14);
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
    if (format != 1)
        throw std::runtime_error("only uncompressed PCM WAV is supported");
    if (bits != 16)
        throw std::runtime_error("only 16-bit PCM WAV is supported");
    if (channels < 1 || block_align != channels * 2)
        throw std::runtime_error("bad channel count / block alignment");
    if (rate != 22050 && rate != 44100)
        throw std::runtime_error("unsupported sample rate " +
                                 std::to_string(rate) +
                                 " Hz (need 22050 or 44100)");

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

    if (rate == 44100) {
        /* 2:1 decimation: anti-alias lowpass, then drop every 2nd sample.
         * Signal of interest is <= 2300 Hz; a 63-tap FIR with 5 kHz
         * cutoff gives plenty of stopband rejection above 11025 Hz. */
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
    }

    if (info_out) {
        char buf[160];
        snprintf(buf, sizeof buf,
                 "%u Hz, %u ch, 16-bit PCM, %.1f s%s",
                 rate, channels, (double)frames / rate,
                 rate == 44100 ? " (decimated 2:1 to 22050 Hz)" : "");
        *info_out = buf;
    }
    return mono;
}
