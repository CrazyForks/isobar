/* reacq-test - the tracker must be able to LEAVE a phase that has gone wrong.
 * Registered with ctest; run via `ctest --test-dir build -R reacq-test`.
 *
 * A networked SDR (KiwiSDR, a remote stream) that drops a buffer resumes
 * mid-line: everything after the gap arrives shifted by however many samples
 * were lost, and the sync position moves with it. The decoder has to find the
 * new position. It cannot do that by tracking - the shift is far larger than
 * any of the narrow searches reach - so this is the one situation where the
 * phase has to be abandoned rather than adjusted.
 *
 * Two ways out exist, and this test exercises both:
 *
 *   - sync_step_lock searches the WHOLE line and acts on the result only if
 *     the next line agrees at the same phase (picture content does not repeat
 *     that way, a sync pulse does). This is the fast path and needs no
 *     release: it snaps on the very line the step happens.
 *   - failing that, a release eventually widens re-acquisition from the
 *     neighbourhood of the held phase back to the whole line, after
 *     widen_after lines with no lock at all.
 *
 * Sessions 27 and 28 left an open worry that neither could fire in practice -
 * that a fallback which keeps succeeding would hold unlocked_for at 0 so the
 * widen was never reached, leaving a wrong phase self-sustaining. Measured
 * against the real recordings that turned out not to be so (widen is reached
 * on 49 lines of the full himawari reception and 27 of jmh-phasing-16k), and
 * sync_step_lock recovers from an injected step of any size within a line or
 * two with no release at all. This test is what keeps that true.
 *
 * The video is spliced rather than the audio: dropping samples of demodulated
 * video is exactly what a dropped buffer does to the sync position, and it
 * needs no new fixture.
 */
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/wavfile.h"

#include <cstdio>
#include <string>
#include <vector>

static const int LINE = 4000;      /* one 120-rpm line, as everywhere else */

/* A correctly phased line has its sync pulse rotated to index 0, so its
 * leading pixels are dark. Same physical check phasing-test uses - it needs
 * no reference image and no per-line internals. */
static bool phased(const std::vector<uint8_t> &line)
{
    const int LEAD = 30, DARK = 60;
    long sum = 0;
    for (int i = 0; i < LEAD; i++)
        sum += line[i];
    return sum / LEAD <= DARK;
}

/* Lines `from`..end that do NOT start on the sync pulse. */
static int misphased(const FaxImage &img, size_t from)
{
    int bad = 0;
    for (size_t r = from; r < img.lines.size(); r++)
        if (!phased(img.lines[r]))
            bad++;
    return bad;
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

    /* The satellite fixture on purpose: a photo is the hard case, because
     * cloud gives the whole-line search plenty of dark rivals to the pulse. */
    std::vector<uint8_t> base = fm_decode(audio, nullptr);

    /* Undisturbed, this fixture phases every line - so any mis-phased line
     * below is caused by the splice and nothing else. */
    FaxImage clean = scan_lines(base);
    int clean_bad = misphased(clean, 0);
    printf("  undisturbed: %zu lines, %d mis-phased\n",
           clean.lines.size(), clean_bad);
    if (clean_bad > 2) {
        printf("FAIL: %d mis-phased lines before any splice - this test "
               "cannot attribute anything to the step\n", clean_bad);
        return 1;
    }

    const int AT = 40;             /* splice here (line) */
    const int shifts[] = { 200, 800, 2000 };
    const int gaps[] = { 0, 40 };  /* dead lines before the step */

    for (int gap : gaps) {
        for (int shift : shifts) {
            /* `gap` lines of dead carrier - no dark run anywhere, so no
             * edge and no fallback, which forces a release - then drop
             * `shift` samples so the signal resumes at a new phase. */
            std::vector<uint8_t> v;
            v.reserve(base.size() + (size_t)gap * LINE);
            long cut = (long)AT * LINE;
            v.insert(v.end(), base.begin(), base.begin() + cut);
            v.insert(v.end(), (size_t)gap * LINE, (uint8_t)200);
            v.insert(v.end(), base.begin() + cut + shift, base.end());

            FaxImage img = scan_lines(v);

            /* Allow 5 lines to notice and recover, then every remaining
             * line must be phased on the NEW position. */
            size_t from = (size_t)(AT + gap + 5);
            if (from >= img.lines.size()) {
                printf("FAIL: spliced decode too short\n");
                return 1;
            }
            int bad = misphased(img, from);
            int checked = (int)(img.lines.size() - from);
            printf("  gap %2d lines, step %4d samples -> %d of %d lines "
                   "mis-phased after recovery (locked %d, relocks %d)\n",
                   gap, shift, bad, checked, img.lines_locked, img.relocks);

            /* Locally 0 in every combination. The bound leaves room for a
             * line or two of disagreement on another architecture, and is
             * still nowhere near the "stuck at the old phase" failure,
             * which mis-phases every line to the end of the recording. */
            if (bad > 4) {
                printf("FAIL: %d of %d lines still mis-phased %d lines after "
                       "a %d-sample step - the tracker did not leave the old "
                       "phase\n", bad, checked, 5, shift);
                return 1;
            }
        }
    }

    printf("reacq-test: PASS\n");
    return 0;
}
