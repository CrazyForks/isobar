/* wavrate-test - core/wavfile.cpp accepts any sample rate.
 * Registered with ctest; run via `ctest --test-dir build -R wavrate-test`.
 *
 * The reader used to accept only 22050 and 44100 Hz, which rejected most
 * SDR recordings (HDSDR writes 12000 Hz, others 8000/48000). It now
 * resamples anything from 6000 Hz up to the decoder's 22050 Hz, via the
 * same core/resample.h Resampler the live-audio path uses.
 *
 * Each case writes a real WAV of a known tone, reads it back, and checks
 * the length and that the tone survived at the right frequency.
 */
#include "../core/wavfile.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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

/* 16-bit mono WAV holding `secs` of a `freq` Hz sine at `rate` Hz. */
static bool write_tone(const std::string &path, uint32_t rate, double freq,
                       double secs)
{
    uint32_t frames = (uint32_t)(rate * secs);
    std::vector<uint8_t> d;
    d.reserve(44 + frames * 2);
    const char *riff = "RIFF", *wave = "WAVEfmt ", *data = "data";
    d.insert(d.end(), riff, riff + 4);
    put_u32(d, 36 + frames * 2);
    d.insert(d.end(), wave, wave + 8);
    put_u32(d, 16);                 /* fmt chunk size  */
    put_u16(d, 1);                  /* PCM             */
    put_u16(d, 1);                  /* mono            */
    put_u32(d, rate);
    put_u32(d, rate * 2);           /* byte rate       */
    put_u16(d, 2);                  /* block align     */
    put_u16(d, 16);                 /* bits            */
    d.insert(d.end(), data, data + 4);
    put_u32(d, frames * 2);
    for (uint32_t i = 0; i < frames; i++) {
        double t = (double)i / rate;
        int16_t s = (int16_t)(20000.0 * std::sin(2.0 * PI * freq * t));
        put_u16(d, (uint16_t)s);
    }
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    size_t n = fwrite(d.data(), 1, d.size(), f);
    fclose(f);
    return n == d.size();
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
