/* slew-test - the sync strip must stay one solid band, even where the
 * transmission's sync position genuinely steps.
 * Registered with ctest; run via `ctest --test-dir build -R slew-test`.
 *
 * `jmh-slew-12k.wav` is a 60-second excerpt (50..110 s) of the same 12 kHz
 * off-air JMH recording that `jmh-offair-12k.wav` comes from. That fixture
 * is cut from the calm opening minute; this one is cut from a stretch where
 * the shape check keeps failing and the fallback keeps firing, which is
 * where the regression lives:
 *
 *   the sync strip is rotated to index 0 - the seam where a line wraps -
 *   so a phase error of N samples does not shift the line, it SPLITS the
 *   strip, putting N samples of black at the FAR end of the line
 *
 * What actually happens on this excerpt: at line 15 the true sync position
 * steps from 2131 to 1969 in ONE line and stays there for the rest of the
 * recording - a real discontinuity in the signal, 162 samples, further than
 * either the shape check (+-search_win = 20) or the fallback
 * (+-fallback_win/2 = 80) can reach. The decoder used to spend a dozen lines
 * crawling or lurching toward it, and every one of those lines had the strip
 * split. `sync_step_lock()` in core/syncscan.h follows the step in one line
 * once the next line confirms it; `sync_slew()` rate-limits everything else.
 * 12 of 119 line pairs used to move the strip by more than 10 px.
 *
 * The check is physical rather than a byte-exact reference, like
 * phasing-test: find the darkest 60-px window near index 0 on each line and
 * measure how far it moved from the line above. A whole, tracked strip
 * barely moves; a split one jumps. Two moves survive and should: they are the
 * genuine step itself, which costs one line each and cannot be avoided
 * causally. Bounds are loose because the 12 kHz input runs core/resample and
 * the exact count can shift by a line or two between architectures - the
 * fixed code sits at 2 and the old code at 12, so the bound is set between
 * them with room either side.
 */
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/wavfile.h"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
    const char *path = "jmh-slew-12k.wav";
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
    /* The step is followed by the fallback path, so some correction must
     * still happen; if it stops, the test has quietly stopped testing. */
    if (img.lines_corrected < 2) {
        printf("FAIL: only %d corrected lines - this excerpt is supposed "
               "to exercise the fallback\n", img.lines_corrected);
        return 1;
    }
    /* Losing the lock through the step would also "fix" the jump count. */
    if (img.lines_locked < 100) {
        printf("FAIL: only %d locked lines, expected >= 100 - the step "
               "cost the lock instead of being followed\n",
               img.lines_locked);
        return 1;
    }

    const int W = FaxImage::WIDTH;
    const int SW = 60;    /* px of sync strip: 20 ms of the 500 ms line */
    const int NEAR = 45;  /* look this far either side of index 0       */
    const int DARK = 60;  /* window mean above this = not the strip     */

    /* Strip position per line, taken cyclically so a strip straddling the
     * line end is found where it really is (negative = wrapped past 0). */
    std::vector<int> pos(img.lines.size(), NEAR + 1);
    for (size_t r = 0; r < img.lines.size(); r++) {
        const std::vector<uint8_t> &L = img.lines[r];
        long best = -1;
        int bestd = 0;
        for (int d = -NEAR; d <= NEAR; d++) {
            long s = 0;
            for (int i = 0; i < SW; i++)
                s += L[((d + i) % W + W) % W];
            if (best < 0 || s < best) { best = s; bestd = d; }
        }
        if (best / SW <= DARK)
            pos[r] = bestd;
    }

    int pairs = 0, jumped = 0, worst = 0;
    for (size_t r = 1; r < pos.size(); r++) {
        if (pos[r] > NEAR || pos[r - 1] > NEAR)
            continue;                    /* no strip found on one of them */
        int d = pos[r] - pos[r - 1];
        if (d < 0) d = -d;
        pairs++;
        if (d > 10) jumped++;
        if (d > worst) worst = d;
    }
    printf("  %d line pairs with a strip on both; %d moved it more than "
           "10 px (worst %d px)\n", pairs, jumped, worst);

    if (pairs < 100) {
        printf("FAIL: only %d usable line pairs, expected >= 100 - the "
               "strip is not being found\n", pairs);
        return 1;
    }
    if (jumped > 6) {
        printf("FAIL: %d line pairs move the sync strip more than 10 px "
               "(12 before the fix, 2 after - the 2 are the recording's own "
               "one-line step) - the strip is breaking across the line seam "
               "again\n", jumped);
        return 1;
    }

    printf("slew-test: PASS\n");
    return 0;
}
