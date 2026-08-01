/* offair-test - decode a real weak-signal, non-44100 Hz recording.
 * Registered with ctest; run via `ctest --test-dir build -R offair-test`.
 *
 * `jmh-offair-12k.wav` is a 60-second excerpt of a 12 kHz off-air JMH
 * reception (HDSDR, 13987 kHz, 2026-08-01). It covers two gaps the other
 * test recordings leave open, and both are cross-platform risks:
 *
 *  1. It is the only sample that is NOT 22050/44100 Hz, so it is the only
 *     one that runs core/resample.cpp inside the WAV reader. That is
 *     floating-point filter + interpolation work, and CI builds it for
 *     x86_64 and aarch64 alike.
 *  2. It is the only sample weak enough to exercise the fallback sync
 *     correction and a re-acquisition. The clean excerpt locks every line
 *     on the shape check and leaves that path entirely untested.
 *
 * Bounds are deliberately loose: the exact locked/corrected split may
 * shift by a line or two between architectures, which is fine. What must
 * hold is that the resampler produces the right amount of audio, sync is
 * acquired, and the fallback path both runs and succeeds.
 */
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/wavfile.h"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
    const char *path = "jmh-offair-12k.wav";
    std::string info;
    std::vector<double> audio;
    try {
        audio = wav_read_22050(path, &info);
    } catch (const std::exception &e) {
        printf("FAIL: cannot read %s: %s\n", path, e.what());
        return 1;
    }
    printf("  %s: %s\n", path, info.c_str());

    /* 60 s at 22050 Hz after resampling from 12000 Hz; allow a handful of
     * samples either way for filter warm-up and the pick-off phase. */
    long want = 60 * 22050;
    long diff = (long)audio.size() - want;
    if (diff < 0) diff = -diff;
    if (diff > 64) {
        printf("FAIL: resampled to %zu samples, expected ~%ld\n",
               audio.size(), want);
        return 1;
    }

    std::vector<uint8_t> video = fm_decode(audio, nullptr);
    FaxImage img = scan_lines(video);

    printf("  %zu lines (locked %d, corrected %d, coasted %d, relocks %d)\n",
           img.lines.size(), img.lines_locked, img.lines_corrected,
           img.lines_coasted, img.relocks);

    /* 60 s / 0.5 s per line, on the fixed grid: exactly 120. */
    if (img.lines.size() != 120) {
        printf("FAIL: %zu lines, expected 120\n", img.lines.size());
        return 1;
    }
    /* Sync must be acquired on the great majority of lines... */
    if (img.lines_locked < 80) {
        printf("FAIL: only %d lines locked, expected >= 80\n",
               img.lines_locked);
        return 1;
    }
    /* ...the fallback must actually run AND succeed on this recording
     * (locally 24 corrected; this is the path the clean sample misses)... */
    if (img.lines_corrected < 5) {
        printf("FAIL: only %d corrected lines, expected >= 5 - the "
               "fallback path is what this recording exists to cover\n",
               img.lines_corrected);
        return 1;
    }
    /* ...and nothing should be left coasting on a signal this good. */
    if (img.lines_coasted > 5) {
        printf("FAIL: %d coasted lines, expected <= 5\n", img.lines_coasted);
        return 1;
    }

    printf("offair-test: PASS\n");
    return 0;
}
