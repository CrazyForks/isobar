/* phasing-test - the sync phase must survive an all-dark preamble.
 * Registered with ctest; run via `ctest --test-dir build -R phasing-test`.
 *
 * `jmh-phasing-16k.wav` is a 90-second excerpt (22..112 s) of a 16 kHz
 * recording of two back-to-back JMH HIMAWARI IR charts. It is the only
 * fixture that contains a full transmission preamble, and that is what it
 * exists to cover:
 *
 *   picture -> ~33 s of ALL-DARK phasing lines -> picture again
 *
 * An all-dark line has no dark RUN of 100..400 samples (the whole line is
 * one 4000-sample dark run), so no sync edge is found, the fallback finds
 * no dip either, and after max_coast lines the lock is released. What
 * happens next is the regression this test guards: re-acquisition used to
 * search the whole 4000-sample line and would re-lock onto a dark feature
 * in the picture, rotating the image sideways by up to 3200 samples for
 * tens of lines. The true sync position in this recording never moves.
 *
 * The check is physical rather than a byte-exact reference: a correctly
 * phased line has its sync pulse rotated to index 0, so the leading pixels
 * of every settled picture line must be dark. Before the fix 23 of the 50
 * lines checked were bright there; after it, none.
 *
 * Bounds are loose where the exact locked/corrected split could shift by a
 * line or two between architectures (the 16 kHz input runs core/resample),
 * and tight only on the thing that must never regress.
 */
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/wavfile.h"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
    const char *path = "jmh-phasing-16k.wav";
    std::string info;
    std::vector<double> audio;
    try {
        audio = wav_read_22050(path, &info);
    } catch (const std::exception &e) {
        printf("FAIL: cannot read %s: %s\n", path, e.what());
        return 1;
    }
    printf("  %s: %s\n", path, info.c_str());

    std::vector<uint8_t> video = fm_decode(audio, nullptr);
    FaxImage img = scan_lines(video);

    printf("  %zu lines (locked %d, corrected %d, coasted %d, relocks %d)\n",
           img.lines.size(), img.lines_locked, img.lines_corrected,
           img.lines_coasted, img.relocks);

    /* 90 s / 0.5 s per line, on the fixed grid: exactly 180. */
    if (img.lines.size() != 180) {
        printf("FAIL: %zu lines, expected 180\n", img.lines.size());
        return 1;
    }
    /* The regression proper, checked first so a failure names the real
     * defect: on the settled picture after the preamble, the sync pulse
     * must sit at index 0 of every line. */
    const int LEAD = 30;          /* px of sync strip at the line start */
    const int DARK = 60;          /* mean above this = not the sync pulse */
    int bright = 0;
    for (size_t r = 130; r < img.lines.size(); r++) {
        long sum = 0;
        for (int i = 0; i < LEAD; i++)
            sum += img.lines[r][i];
        if (sum / LEAD > DARK)
            bright++;
    }
    if (bright > 3) {
        printf("FAIL: %d picture lines do not start on the sync pulse - "
               "the phase ran away over the all-dark preamble\n", bright);
        return 1;
    }
    printf("  %d of 50 settled picture lines mis-phased (was 23 before "
           "the fix)\n", bright);

    /* The preamble is ~66 all-dark lines, so a large coasted count is
     * correct here - but the picture either side of it must lock. */
    if (img.lines_locked < 65) {
        printf("FAIL: only %d lines locked, expected >= 65\n",
               img.lines_locked);
        return 1;
    }
    /* One release at the preamble is expected. Re-locking over and over
     * means the phase is wandering again. */
    if (img.relocks > 3) {
        printf("FAIL: %d relocks, expected <= 3 - the phase is wandering "
               "after the all-dark preamble\n", img.relocks);
        return 1;
    }

    printf("phasing-test: PASS\n");
    return 0;
}
