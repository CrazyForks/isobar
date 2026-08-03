/* satellite-test - the sync tracker must hold phase on a PICTURE, not a chart.
 * Registered with ctest; run via `ctest --test-dir build -R satellite-test`.
 *
 * `jmh-himawari-12k.wav` is a 60-second excerpt (300..360 s) of a KiwiSDR
 * reception of a JMH HIMAWARI IR satellite chart, resampled to 12 kHz. Every
 * other fixture is a test chart or a chart-like transmission, and charts are
 * easy: their sync pulse is the only really dark thing on the line. A
 * satellite photo is not, and that difference broke the tracker twice:
 *
 *  1. Lock acquisition took the first edge that sustained a chain of
 *     ~4000-sample periods. Cloud edges in a photo repeat line to line
 *     closely enough to sustain a chain of their own, and being earlier in
 *     the line they beat the real pulse. The decoder locked 339 samples off
 *     true and never recovered (session 26; fixed by sync_dark_floor).
 *  2. The phase was anchored on ONE sample - where the moving average first
 *     crosses dark_th. On a photo the picture abutting the pulse moves that
 *     edge, so the phase wobbled line to line and the picture combed. Fixed
 *     by sync_anchor, which anchors on the darkest fallback_win-wide window
 *     instead (session 28).
 *
 * This excerpt separates all three states sharply - measured, by building
 * each commit and running this very test against it:
 *
 *   before sync_dark_floor (1b14b98)   58 locked, 62 corrected, 2 relocks
 *   before sync_anchor     (604e055)   77 locked, 43 corrected, 0 relocks
 *   current                           120 locked,  0 corrected, 0 relocks
 *
 * and 31 adjacent line pairs visibly offset from each other before
 * sync_anchor against 1 after. The graticule line crossing this stretch is
 * continuous in the current decode and dashed in both older ones.
 *
 * Bounds are set well between those two, so an architecture that resamples a
 * hair differently still passes while either regression fails loudly.
 */
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/wavfile.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

/* How far line `b` has to be shifted sideways to sit on top of line `a`.
 *
 * This is the combing the eye actually sees, measured on the output image
 * and nothing else. It is deliberately NOT a search for the darkest window:
 * on a satellite picture the darkest thing in a line is usually cloud, not
 * the sync pulse, which is what made the session-28 tracking numbers
 * untrustworthy until they were re-measured against a folded reference.
 *
 * Columns 300..1400 only - that skips the sync strip itself and the bright
 * left margin, so what is compared is picture against picture. */
static int best_shift(const std::vector<uint8_t> &a,
                      const std::vector<uint8_t> &b)
{
    const int RANGE = 20, LO = 300, HI = 1400;
    long best = -1;
    int best_s = 0;
    for (int s = -RANGE; s <= RANGE; s++) {
        long tot = 0;
        for (int i = LO; i < HI; i++) {
            int j = i + s;
            tot += abs((int)a[i] - (int)b[j]);
        }
        if (best < 0 || tot < best) {
            best = tot;
            best_s = s;
        }
    }
    return best_s;
}

int main()
{
    const char *path = "jmh-himawari-12k.wav";
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

    /* 60 s / 0.5 s per line, on the fixed grid: exactly 120. */
    if (img.lines.size() != 120) {
        printf("FAIL: %zu lines, expected 120\n", img.lines.size());
        return 1;
    }

    /* The shape check must find the real pulse on nearly every line. This
     * stretch locks all 120 now; before sync_anchor it managed 77, and
     * before sync_dark_floor 58 (see the table above). */
    if (img.lines_locked < 110) {
        printf("FAIL: only %d lines locked, expected >= 110 - the tracker is "
               "not holding the sync pulse on picture-heavy video\n",
               img.lines_locked);
        return 1;
    }
    /* Falling back this often means the shape check is losing the pulse. */
    if (img.lines_corrected > 8) {
        printf("FAIL: %d corrected lines, expected <= 8 - the shape check is "
               "missing the pulse and the fallback is carrying the decode\n",
               img.lines_corrected);
        return 1;
    }
    /* The signal is solid here; nothing should be coasting, and a relock
     * means the phase was lost on a picture it should have tracked. */
    if (img.lines_coasted > 5) {
        printf("FAIL: %d coasted lines, expected <= 5\n", img.lines_coasted);
        return 1;
    }
    if (img.relocks > 1) {
        printf("FAIL: %d relocks, expected <= 1\n", img.relocks);
        return 1;
    }

    /* And the picture itself: adjacent lines must sit on top of each other.
     * This is the check that would have caught session 28's combing, which
     * the counters above miss - a line can be "locked" and still be a few
     * pixels out. */
    int comb3 = 0, comb10 = 0;
    for (size_t r = 1; r < img.lines.size(); r++) {
        int s = best_shift(img.lines[r - 1], img.lines[r]);
        if (s < 0) s = -s;
        if (s > 3)  comb3++;
        if (s > 10) comb10++;
    }
    printf("  line pairs offset from each other: %d > 3 px, %d > 10 px\n",
           comb3, comb10);

    /* Locally 1 and 0; before sync_anchor, 31 and 5. */
    if (comb3 > 12) {
        printf("FAIL: %d adjacent line pairs more than 3 px apart, expected "
               "<= 12 - the picture is combing\n", comb3);
        return 1;
    }
    if (comb10 > 3) {
        printf("FAIL: %d adjacent line pairs more than 10 px apart, expected "
               "<= 3\n", comb10);
        return 1;
    }

    printf("satellite-test: PASS\n");
    return 0;
}
