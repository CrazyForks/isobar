/* phase-lock-test - sync lock/re-lock at a NONZERO phase (core/syncscan.cpp).
 * Registered with ctest; run via `ctest --test-dir build -R phase-lock-test`.
 *
 * (audit 2026-08-06 sec. 3.2/3.5.4) Every committed fixture happens to
 * settle near phase 0, so the least-tested real-world path is a sync pulse
 * sitting well off the stream grid. This synthetic video pins it:
 *
 *   lines  0..19  pulse at sample 1500  -> lock at phi ~1500
 *   lines 20..31  dead (all bright)     -> release after max_coast misses
 *   lines 32..59  pulse at sample 2500  -> step-lock snap, then re-lock
 *
 * Clean (noiseless) video keeps the assertions deterministic; noisy
 * real-world content is what the five recorded fixtures cover.
 */
#include "../core/syncscan.h"

#include <cstdio>
#include <vector>

static int fail(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

static const int LINE = 4000;      /* one 120-rpm line @ 8000 S/s */
static const int NLINES = 60;
static const uint8_t BRIGHT = 200, DARK = 10, PULSE = 150;

/* emitted line has the sync pulse rotated to the line start: dark pixels
 * up front, none in the picture body */
static int pulse_at_start(const std::vector<uint8_t> &px)
{
    int dark_front = 0;
    for (int i = 0; i < 100; i++)
        if (px[i] < 96)
            dark_front++;
    if (dark_front < 30)
        return 0;
    for (int i = 200; i < 1400; i++)
        if (px[i] < 96)
            return 0;
    return 1;
}

int main()
{
    std::vector<uint8_t> video(NLINES * LINE, BRIGHT);
    for (int l = 0; l < NLINES; l++) {
        int at = l < 20 ? 1500 : (l < 32 ? -1 : 2500);
        if (at < 0)
            continue;                     /* dead stretch: no pulse */
        for (int i = 0; i < PULSE; i++)
            video[l * LINE + at + i] = DARK;
    }

    FaxImage img = scan_lines(video);
    if ((int)img.lines.size() != NLINES)
        return fail("line count");

    /* first segment: lock at phi ~1500 (nonzero), held through it */
    if (img.lines_locked < 40)
        return fail("too few locked lines");
    if (!pulse_at_start(img.lines[10]))
        return fail("locked line does not start at the pulse (phi ~1500)");

    /* dead stretch: coasted, lock released (12 > max_coast) */
    if (img.lines_coasted < 10)
        return fail("dead stretch not coasted");

    /* after the dead stretch the pulse sits 1000 samples off the old
     * phase: step-lock must snap to it and the tracker must RE-lock */
    if (img.relocks != 1)
        return fail("expected exactly one re-lock");
    if (!pulse_at_start(img.lines[45]))
        return fail("line after the step not aligned");
    if (!pulse_at_start(img.lines[NLINES - 1]))
        return fail("re-lock did not land on the new phase (phi ~2500)");

    printf("phase-lock-test: PASS (locked %d, corrected %d, coasted %d, "
           "relocks %d)\n", img.lines_locked, img.lines_corrected,
           img.lines_coasted, img.relocks);
    return 0;
}
