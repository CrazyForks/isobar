/* wavrate-test - core/wavfile.cpp accepts any sample rate, and PCM however
 * the fmt chunk spells it.
 * Registered with ctest; run via `ctest --test-dir build -R wavrate-test`.
 *
 * The reader used to accept only 22050 and 44100 Hz, which rejected most
 * SDR recordings (HDSDR writes 12000 Hz, others 8000/48000). It now
 * resamples anything from 6000 Hz up to the decoder's 22050 Hz, via the
 * same core/resample.h Resampler the live-audio path uses.
 *
 * Each case writes a real WAV of a known tone, reads it back, and checks
 * the length and that the tone survived at the right frequency.
 *
 * The last case covers WAVE_FORMAT_EXTENSIBLE (tag 0xFFFE), which macOS
 * `afconvert -f WAVE` emits for any source with a channel layout — every
 * .m4a, so every phone recording converted for this project. The reader
 * used to reject those as "not PCM" even though the samples are plain PCM.
 */
#include "../core/wavfile.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

/* Local PI — see core/tonedetect.cpp (M_PI is missing under MSVC unless
 * _USE_MATH_DEFINES is defined before the math headers). */
static const double PI = 3.14159265358979323846;

static int fail(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

static void put_u32(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back((uint8_t)(x & 0xFF));       v.push_back((uint8_t)(x >> 8 & 0xFF));
    v.push_back((uint8_t)(x >> 16 & 0xFF)); v.push_back((uint8_t)(x >> 24 & 0xFF));
}

static void put_u16(std::vector<uint8_t> &v, uint16_t x)
{
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)(x >> 8 & 0xFF));
}

/* 16-bit mono WAV holding `secs` of a `freq` Hz sine at `rate` Hz.
 *
 * With `extensible`, the fmt chunk is written the way macOS `afconvert
 * -f WAVE` writes it for any source carrying a channel layout: 40 bytes,
 * tag 0xFFFE, and the real format tag buried in the SubFormat GUID. The
 * samples are identical either way, so both spellings must decode alike. */
static bool write_tone(const std::string &path, uint32_t rate, double freq,
                       double secs, bool extensible = false)
{
    uint32_t frames = (uint32_t)(rate * secs);
    uint32_t fmt_size = extensible ? 40 : 16;
    std::vector<uint8_t> d;
    d.reserve(28 + fmt_size + frames * 2);
    const char *riff = "RIFF", *wave = "WAVEfmt ", *data = "data";
    d.insert(d.end(), riff, riff + 4);
    put_u32(d, 20 + fmt_size + frames * 2);
    d.insert(d.end(), wave, wave + 8);
    put_u32(d, fmt_size);
    put_u16(d, extensible ? 0xFFFE : 1);
    put_u16(d, 1);                  /* mono            */
    put_u32(d, rate);
    put_u32(d, rate * 2);           /* byte rate       */
    put_u16(d, 2);                  /* block align     */
    put_u16(d, 16);                 /* bits            */
    if (extensible) {
        put_u16(d, 22);             /* cbSize                    */
        put_u16(d, 16);             /* valid bits per sample     */
        put_u32(d, 0x4);            /* channel mask: front centre */
        put_u16(d, 1);              /* SubFormat = PCM ...        */
        static const uint8_t ks_suffix[14] = {   /* ... + the fixed tail */
            0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
            0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
        };
        d.insert(d.end(), ks_suffix, ks_suffix + 14);
    }
    d.insert(d.end(), data, data + 4);
    put_u32(d, frames * 2);
    for (uint32_t i = 0; i < frames; i++) {
        double t = (double)i / rate;
        int16_t s = (int16_t)(20000.0 * std::sin(2.0 * PI * freq * t));
        put_u16(d, (uint16_t)s);
    }
    /* ofstream, not fopen: MSVC flags fopen as C4996 and the build must
     * stay warning-free on all five CI runners. */
    std::ofstream f(path.c_str(), std::ios::out | std::ios::binary);
    if (!f)
        return false;
    f.write((const char *)d.data(), (std::streamsize)d.size());
    return (bool)f;
}

/* Dominant frequency by zero-crossing count - enough to prove the tone
 * came through the resampler intact without pulling in the FFT. */
static double dominant_freq(const std::vector<double> &x, double rate)
{
    long crossings = 0;
    for (size_t i = 1; i < x.size(); i++)
        if ((x[i - 1] < 0.0) != (x[i] < 0.0))
            crossings++;
    return crossings * rate / (2.0 * (double)x.size());
}

int main()
{
    namespace fs = std::filesystem;
    std::string tmp = fs::temp_directory_path().string();
    const double SECS = 2.0, TONE = 1900.0;   /* 1900 Hz = WEFAX centre */

    /* 8000 is a common SDR rate, 12000 is what the HDSDR sample uses,
     * 22050 passes through untouched, 44100 takes the exact 2:1 path,
     * 48000 is the usual sound-card rate. */
    const uint32_t rates[] = { 8000, 12000, 22050, 44100, 48000 };

    for (uint32_t rate : rates) {
        std::string path = tmp + "/isobar-wavrate-" + std::to_string(rate) + ".wav";
        if (!write_tone(path, rate, TONE, SECS))
            return fail("cannot write test WAV");

        std::string info;
        std::vector<double> out;
        try {
            out = wav_read_22050(path, &info);
        } catch (const std::exception &e) {
            printf("FAIL: %u Hz rejected: %s\n", rate, e.what());
            return 1;
        }

        /* length: SECS at 22050, allowing a few samples of filter warm-up */
        size_t want = (size_t)(22050.0 * SECS);
        long diff = (long)out.size() - (long)want;
        if (diff < 0) diff = -diff;
        if (diff > 64) {
            printf("FAIL: %u Hz -> %zu samples, expected ~%zu\n",
                   rate, out.size(), want);
            return 1;
        }

        double got = dominant_freq(out, 22050.0);
        if (std::fabs(got - TONE) > 40.0) {
            printf("FAIL: %u Hz -> tone came back at %.0f Hz, expected %.0f\n",
                   rate, got, TONE);
            return 1;
        }
        printf("  %5u Hz -> %6zu samples, tone %.0f Hz   [%s]\n",
               rate, out.size(), got, info.c_str());
        std::remove(path.c_str());
    }

    /* WAVE_FORMAT_EXTENSIBLE: same samples, fancier fmt chunk, so the
     * decoded result must match the plain spelling to the last sample. */
    {
        std::string plain = tmp + "/isobar-wavfmt-plain.wav";
        std::string ext   = tmp + "/isobar-wavfmt-ext.wav";
        if (!write_tone(plain, 12000, TONE, SECS) ||
            !write_tone(ext, 12000, TONE, SECS, true))
            return fail("cannot write extensible test WAVs");

        std::string info;
        std::vector<double> a, b;
        try {
            a = wav_read_22050(plain, 0);
            b = wav_read_22050(ext, &info);
        } catch (const std::exception &e) {
            printf("FAIL: extensible WAV rejected: %s\n", e.what());
            return 1;
        }
        if (a != b) {
            printf("FAIL: extensible decode differs (%zu vs %zu samples)\n",
                   a.size(), b.size());
            return 1;
        }
        printf("  extensible (0xFFFE) -> %6zu samples, identical   [%s]\n",
               b.size(), info.c_str());

        /* A SubFormat GUID that is not KSDATAFORMAT_SUBTYPE means the data
         * is something we have not been told how to read: reject it rather
         * than trust the two bytes that happen to sit at the front. */
        std::fstream patch(ext.c_str(),
                           std::ios::in | std::ios::out | std::ios::binary);
        patch.seekp(46);            /* first byte of the GUID's fixed tail:
                                     * 20 (fmt body) + 24 (GUID) + 2 (tag) */
        patch.put((char)0x99);
        patch.close();
        bool threw = false;
        try {
            wav_read_22050(ext, 0);
        } catch (const std::exception &) {
            threw = true;
        }
        std::remove(plain.c_str());
        std::remove(ext.c_str());
        if (!threw)
            return fail("bad SubFormat GUID accepted; should be rejected");
    }

    /* Below the 6000 Hz floor there is no Nyquist room for the 2300 Hz
     * white subcarrier: reject loudly rather than decode nonsense. */
    {
        std::string path = tmp + "/isobar-wavrate-4000.wav";
        if (!write_tone(path, 4000, 1000.0, 0.5))
            return fail("cannot write low-rate WAV");
        bool threw = false;
        try {
            wav_read_22050(path, 0);
        } catch (const std::exception &) {
            threw = true;
        }
        std::remove(path.c_str());
        if (!threw)
            return fail("4000 Hz accepted; should be rejected as too low");
    }

    printf("wavrate-test: PASS\n");
    return 0;
}
