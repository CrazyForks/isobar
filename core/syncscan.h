/* syncscan.h - WEFAX line sync detection and image assembly.
 *
 * Maps docs/01-program-analysis.md sections 3.2(7)(8)(10) and 4:
 *   one 120-rpm line = 4000 samples at 8000 S/s (0.5 s)
 *   lines are emitted on a FIXED 4000-sample grid from stream start,
 *     rotated by the tracked phase offset (sync edge -> index 0);
 *     the grid never moves, tracking only adjusts the phase
 *   sync pulse: black run, period 3980..4020 samples, width 100..400
 *   fallback: min-brightness search (full window until first lock,
 *     narrow window around the predicted edge afterwards)
 *   4000 samples -> 1500 px via line[8*i/3]
 *
 * Line states (reported via the line_state callback):
 *   0 = locked:    line sits on a shape-detected sync edge
 *   1 = corrected: shape check failed, min-brightness fallback found a
 *                  plausible edge (original's "Sync corr" path)
 *   2 = coasting:  no edge and no valid fallback; phase held
 *                  (after lock_hyst misses; before that the LED stays
 *                  "locked" - release hysteresis)
 *   3 = not locked yet (or Sync button released): unrotated grid line,
 *                  LEDs off (docs/01 sec. 3.2 "Sync-track enable")
 */
#ifndef ISOBAR_SYNCSCAN_H
#define ISOBAR_SYNCSCAN_H

#include <vector>
#include <cstdint>

struct FaxImage {
    static const int WIDTH = 1500;     /* pixels per line                   */
    static const int MAX_LINES = 2280; /* 19 min @ 120 rpm (docs/01 sec. 4) */
    std::vector<std::vector<uint8_t>> lines;  /* each WIDTH px, 0=black */

    /* Stats. Initialised here, not just by scan_lines: the GUI builds
     * FaxImages of its own (the rotate buttons), and copying one with
     * indeterminate counters into app->image is undefined behaviour -
     * gcc catches it as -Wmaybe-uninitialized on this struct. */
    int lines_locked = 0;     /* lines emitted on a detected sync edge */
    int lines_corrected = 0;  /* lines emitted on the fallback search */
    int lines_coasted = 0;    /* lines emitted on predicted position */
    int relocks = 0;          /* times lock was re-acquired after losing it */
};

/* Tunable sync-scan thresholds. The defaults are the original program's
 * own ini defaults, read out of the binary's ReadInteger calls
 * (docs/01-program-analysis.md sec. 6) - except dark_th and fb_thresh,
 * which stay at ours because our detector computes those two quantities
 * differently from the original's (DEVIATIONS.md #16).
 * The GUI's "Details" dialog reaches these via the settings file
 * (docs/01 sec. 5-6):
 *   Sync2Thre -> dark_th       SyncThre  -> fb_thresh
 *   SyncWidth -> search_win    (samples of allowed sync-position jump)
 *   LReSycn   -> max_coast     (invalid lines before lock is dropped)
 *   RReSycn   -> lock_hyst     (valid lines before lock is declared)
 *   syn combo -> fallback_win = 40*(i+1) samples (= 5*(i+1) ms @ 8 kHz)
 * min_pulse/max_pulse are NOT settable: the 100..400-sample sync pulse
 * window is hard-coded in the original (docs/01 sec. 3.2(7)). */
struct SyncParams {
    int min_period;   /* 3980 samples: shortest accepted sync period */
    int max_period;   /* 4020 */
    int min_pulse;    /*  100 samples: shortest sync pulse (12.5 ms) */
    int max_pulse;    /*  400 samples: longest sync pulse (50 ms)    */
    int search_win;   /*   20 samples: +- window for shape-edge match;
                        an edge further than this from the predicted
                        position is rejected (SyncWidth)              */
    int max_coast;    /*   10 lines: give up lock after this many
                        consecutive invalid lines (LReSycn)           */
    int dark_th;      /*   96: brightness below this = "dark"         */
    int lock_hyst;    /*    5: valid periods to lock; misses before
                        "coasting" state (RReSycn hysteresis)         */
    int fallback_win; /*  160 samples: min-brightness search width
                        (syn combo default 20 ms); 0 disables fallback */
    int fb_thresh;    /*   10: min dip depth below window mean for a
                        valid fallback edge - our own second-chance
                        search, tried only when the ported one below
                        declines (cf. SyncThre; DEVIATIONS #16)       */
    int fb_mean;      /*   30: the original's SyncThre, in its own
                        units at last - max boxcar mean (0..255 over
                        the binarised video) for a valid edge on the
                        ported fallback (docs/01 sec. 3.2(8))         */
};

SyncParams sync_default_params();

/* How black a dark run must be, on average, to be allowed to START a lock
 * chain. Shared by scan_lines and LiveScan (the two must stay
 * byte-identical - live-test).
 *
 * Lock acquisition takes the first edge that sustains lock_hyst valid
 * ~4000-sample periods. On a picture-heavy image that is not enough:
 * cloud edges in a satellite photo repeat line to line closely enough to
 * sustain a chain of their own, and because the edge list is in position
 * order, a picture chain EARLIER in the line beats the real sync pulse.
 * Measured on a 17-minute KiwiSDR himawari reception, the decoder locked
 * 339 samples off the true sync on its very first lock and never got back
 * - re-acquisition is confined to the neighbourhood of the phase it
 * already has, so a false lock is self-sustaining.
 *
 * Darkness separates the two cleanly where position does not. Run-mean
 * brightness at the true pulse against every picture chain that competes
 * with it:
 *
 *   recording            real pulse (median/90%)   picture chain (10%/median)
 *   kiwi himawari              12 / 22                    33 / 48
 *   kiwi test chart            19 / 33                    50 / 55
 *   FAXSignal                   3 /  4                    37 / 53
 *   jmh-offair-12k             17 / 26                    (none at all)
 *
 * dark_th/3 keeps 98.4-100% of real pulses on all four and rejects ~91%
 * of picture chains on himawari, ~95% on the test chart. Expressed as a
 * fraction of dark_th rather than a new constant, so the one "how black
 * is black" setting still governs it (Sync2Thre in the Details dialog).
 * This gates the chain START only - the chain's own period test and
 * everything downstream are unchanged. */
inline int sync_dark_floor(const SyncParams &p) { return p.dark_th / 3; }

/* Mean brightness of the dark run STARTING at `pos`, or 255 if there is no
 * dark run there. The same quantity find_lock scores a chain start by,
 * measured at a fallback result instead. Shared by scan_lines and LiveScan.
 *
 * Probed from min_pulse/2 INSIDE the run, not at `pos` itself: both callers
 * publish the bright->dark edge, where the moving average is still crossing
 * dark_th, so sampling exactly there reads a bright value and would reject
 * every genuine pulse (it did - offair-test went to 0 corrected lines). Any
 * run long enough to be a sync pulse covers that probe point. */
inline int sync_run_mean(const std::vector<int> &sm, long pos,
                         const SyncParams &p)
{
    long n = (long)sm.size();
    pos += p.min_pulse / 2;
    if (pos < 0 || pos >= n || sm[pos] >= p.dark_th)
        return 255;
    long lo = pos, hi = pos;
    while (lo > 0 && sm[lo - 1] < p.dark_th && pos - lo < p.max_pulse)
        lo--;
    while (hi + 1 < n && sm[hi + 1] < p.dark_th && hi - pos < p.max_pulse)
        hi++;
    long s = 0;
    for (long i = lo; i <= hi; i++)
        s += sm[i];
    return (int)(s / (hi - lo + 1));
}

/* Refine a sync position from the dark run's leading edge to the steadiest
 * point of the pulse. Shared by scan_lines and LiveScan (live-test).
 *
 * Every detector here publishes the same thing: the first sample where the
 * 8-sample moving average crosses dark_th. That is ONE sample, and it moves
 * with noise and with whatever picture abuts the pulse - which is the
 * per-line wobble still left in the image after the S27 fixes, and what the
 * eye reads as a ragged baseline. The pulse as a whole is far bigger
 * evidence. Line-to-line wobble of the same pulses, de-trended so the
 * transmission's own drift is not counted as jitter:
 *
 *   anchor           off-air 12k (rms/median/90%)   himawari (median/90%)
 *   run start           4.18 / 1.0 / 3.0               2.2 / 42.2
 *   run centre          2.65 / 0.5 / 2.5               1.3 / 21.8
 *   darkest window      0.95 / 1.0 / 2.0               0.8 /  3.8
 *   matched step edge   1.65 / 1.0 / 2.0               1.8 / 37.2
 *
 * So: the start of the darkest fallback_win-wide window. A JMH sync pulse
 * is ~158 samples against the 160 default, so the window has to straddle
 * both of the run's edges to find its minimum - which makes the result a
 * darkness-weighted CENTRE estimate, with the noise on the two edges partly
 * cancelling. A leading-edge estimator cannot do that however much it
 * averages (the matched step above, 50 and 80 samples either side), because
 * on a photo the picture abutting the pulse moves the edge itself.
 *
 * Three bounds keep it honest, all of them about black wider than a pulse -
 * a chart's margin merged with the sync (FAXSignal: 328-sample runs), a
 * black band in a satellite photo:
 *   - the dark run may be at most 2*win wide, else there is no pulse shape
 *     to centre on;
 *   - the minimum must be a real dip and not a tie - inside flat black
 *     every window position is equally dark and the argmin lands
 *     arbitrarily (on FAXSignal that moved the phase 418 samples on a
 *     single line). So the minimum plateau, at 2 grey levels' tolerance,
 *     must be narrower than win/4 and must not touch either end of the
 *     search - a minimum at the boundary means the true one is outside it.
 *     Where it does qualify, the plateau's CENTRE is the anchor, which
 *     removes what little tie is left;
 *   - and the anchor may move at most win/2 from the position published.
 * A move that big does not merely shift the image, it wraps the leading
 * part of the strip to the far end of the line - the split strip of S26.
 *
 * Positions with no dark run (a coasted prediction, the video's end) and
 * anything failing those tests come back unchanged, so every path that
 * cannot be improved behaves exactly as it did before. */
inline long sync_anchor(const std::vector<int> &sm, long pos,
                        const SyncParams &p)
{
    long n = (long)sm.size();
    long win = p.fallback_win > 0 ? p.fallback_win : 160;
    long probe = pos + p.min_pulse / 2;
    if (pos < 0 || probe >= n || sm[probe] >= p.dark_th)
        return pos;

    /* the dark run around the probe, and out if it is too wide to be one
     * pulse (walked no further than the bound, so this stays cheap) */
    long runmax = 2 * win;
    long lo = probe, hi = probe;
    while (lo > 0 && sm[lo - 1] < p.dark_th && probe - lo <= runmax)
        lo--;
    while (hi + 1 < n && sm[hi + 1] < p.dark_th && hi - probe <= runmax)
        hi++;
    if (hi - lo + 1 > runmax)
        return pos;

    /* mean brightness of a win-wide window at every position within win/2
     * of pos (kept as a sum: the comparisons below scale with it) */
    long qlo = pos - win / 2, qhi = pos + win / 2;
    if (qlo < 0) qlo = 0;
    if (qhi > n - win) qhi = n - win;
    if (qlo >= qhi)
        return pos;
    std::vector<long> s((size_t)(qhi - qlo + 1));
    long sum = 0;
    for (long i = qlo; i < qlo + win; i++)
        sum += sm[i];
    s[0] = sum;
    long best = sum;
    for (long q = qlo + 1; q <= qhi; q++) {
        sum += sm[q + win - 1] - sm[q - 1];
        s[(size_t)(q - qlo)] = sum;
        if (sum < best)
            best = sum;
    }

    long first = -1, last = -1;
    for (long q = qlo; q <= qhi; q++)
        if (s[(size_t)(q - qlo)] <= best + 2 * win) {   /* 2 grey levels */
            if (first < 0)
                first = q;
            last = q;
        }
    if (last - first > win / 4 || first == qlo || last == qhi)
        return pos;
    return (first + last) / 2;
}

/* The one unambiguous sync pulse in a line, or -1 if there isn't one.
 *
 * Darkest fallback_win window anywhere in the line, accepted only if it
 * really is a sync pulse and not just the darkest patch of picture:
 *   - the window must be dark (dark_th), and
 *   - the best rival at least 300 samples away must be dark_th/2
 *     brighter - a real sync pulse wins by a mile, picture content does
 *     not, and
 *   - the dark run it sits in must be a plausible sync pulse WIDTH
 *     (min_pulse..max_pulse), the same bound find_sync_edges applies.
 * The width test is what keeps a chart's wide black margin out: a margin
 * is darker than anything and repeats on every line, so neither of the
 * first two tests excludes it on their own.
 * Returns the position as a phase within the line. */
inline long sync_line_pulse(const std::vector<int> &sm, long grid,
                            const SyncParams &p)
{
    const long LINE = 4000;
    int win = p.fallback_win > 0 ? p.fallback_win : 160;
    if (grid < 0 || grid + LINE > (long)sm.size())
        return -1;

    /* mean over a win-wide window at every position, wrapping in-line */
    std::vector<long> m(LINE);
    long sum = 0;
    for (int i = 0; i < win; i++)
        sum += sm[grid + i];
    m[0] = sum / win;
    for (long q = 1; q < LINE; q++) {
        sum += sm[grid + (q + win - 1) % LINE];
        sum -= sm[grid + (q - 1) % LINE];
        m[q] = sum / win;
    }
    long best = 0;
    for (long q = 1; q < LINE; q++)
        if (m[q] < m[best])
            best = q;
    long rival = 255;
    for (long q = 0; q < LINE; q++) {
        long d = q - best < 0 ? best - q : q - best;
        if (d < 300 || d > LINE - 300)
            continue;               /* same pulse, not a rival */
        if (m[q] < rival)
            rival = m[q];
    }
    if (m[best] >= p.dark_th)
        return -1;
    /* The rival margin asks "is this much darker than anything else in the
     * line". That is a chart's question. A satellite photo has large
     * near-black areas, so a cloud 300 samples away is nearly as dark as
     * the pulse and the margin collapses - measured on a KiwiSDR himawari
     * reception, the darkest window IS the real pulse on 93.9% of lines
     * and passes the dark and width tests, but the margin test passes on
     * 1.5% (median margin 24 against the required 48). The whole-line
     * rescue was therefore switched off exactly where it was needed, and
     * the phase wandered instead: 40% of lines more than 10 px off.
     *
     * So accept on absolute darkness as an alternative: a sync pulse is
     * genuinely black (run-mean 12 on himawari, 3 on FAXSignal, 17-19 on
     * the two off-air recordings) while the picture chains that compete
     * with it sit near dark_th/2. This is the same floor find_lock uses
     * on a chain start, for the same reason. It only ADDS acceptances -
     * anything the margin already admitted still is - so chart-like
     * content behaves exactly as before, and the width test above still
     * excludes a chart's wide black margin either way. What keeps the
     * extra acceptances honest is sync_step_lock's next-line agreement,
     * which no dark patch of picture survives twice at the same phase. */
    if (rival - m[best] < p.dark_th / 2 && m[best] >= sync_dark_floor(p))
        return -1;

    /* width: expand the dark run around the darkest sample in the window */
    long dmin = best;
    for (long i = best; i < best + win; i++)
        if (sm[grid + i % LINE] < sm[grid + dmin % LINE])
            dmin = i;
    long lo = dmin, hi = dmin;
    while (hi - lo < LINE && sm[grid + (lo - 1 + LINE) % LINE] < p.dark_th)
        lo--;
    while (hi - lo < LINE && sm[grid + (hi + 1) % LINE] < p.dark_th)
        hi++;
    long run = hi - lo + 1;
    if (run < p.min_pulse || run > p.max_pulse)
        return -1;
    return best;
}

/* Follow a genuine step in the transmission's sync position.
 *
 * The narrow searches above cannot: the shape check reaches +-search_win
 * (20 samples) and the fallback +-fallback_win/2 (80), while a real step
 * is bigger than either. The 12 kHz off-air recording steps 162 samples
 * between one line and the next and then holds the new position for the
 * rest of the reception; the 44.1 kHz sample does the same at line 930
 * where a new transmission starts. Without this the decoder spends ten
 * or more lines crawling toward the new position - and every one of those
 * lines has the sync strip split across the line's two ends, which is
 * what makes those lines unreadable. On a KiwiSDR or any other networked
 * SDR feed, a dropout leaves exactly this kind of step.
 *
 * One line of LOOKAHEAD: the pulse found in THIS line is acted on only if
 * the NEXT line agrees with it. A real sync pulse repeats at the same
 * phase; a dark patch of picture does not. Confirming forwards rather
 * than backwards is what lets the step be followed on the very line it
 * starts on, instead of one line later - which matters because that line
 * is the one whose strip is split. It is also what makes a whole-line
 * search safe at all, where a bare one re-locks onto dark picture content
 * (`phasing-test`, docs/01 sec. 3.2(8)).
 *
 * Needs two whole lines of video from `grid`; callers that do not have
 * them yet must wait (LiveScan) or skip the check (end of a file).
 * Returns the absolute position to snap to, or -1 to leave the phase
 * alone. Shared by scan_lines and LiveScan. */
inline long sync_step_lock(const std::vector<int> &sm, long grid, long phi,
                           const SyncParams &p)
{
    const long LINE = 4000;
    if (grid < 0 || grid + 2 * LINE > (long)sm.size())
        return -1;

    long here = sync_line_pulse(sm, grid, p);
    if (here < 0)
        return -1;
    long next = sync_line_pulse(sm, grid + LINE, p);
    if (next < 0)
        return -1;
    long dc = here - next < 0 ? next - here : here - next;
    if (dc > p.search_win && dc < LINE - p.search_win)
        return -1;              /* the next line does not agree */

    /* only worth acting on if it is somewhere the narrow searches cannot
     * already reach - otherwise the ordinary tracking has it in hand */
    long dp = here - phi < 0 ? phi - here : here - phi;
    if (dp <= p.search_win || dp >= LINE - p.search_win)
        return -1;
    return grid + here;
}

/* Move the line phase toward a fallback result, rate-limited.
 *
 * The sync strip is rotated to index 0 - the seam where a line wraps -
 * so a phase error of N samples does not merely shift the line, it
 * SPLITS the strip: N samples' worth of black appears at the far end.
 * The fallback searches +-fallback_win/2 (80 samples = 30 px = half the
 * strip), four times further than the shape check's +-search_win, and
 * before this rule its result was applied whole and became the centre of
 * the next line's search - so a run of corrected lines random-walked
 * (measured: 146 px on `jmh sample.wav`, 305 px on `FAXSignal.wav`).
 *
 * The original rejects a fallback result further than SyncWidth from the
 * previous position outright (docs/01 sec. 3.2(8)). We cannot: a real
 * transmission does step further than that - the off-air recording moves
 * ~162 samples four times - and rejecting costs more than it saves
 * (measured: mis-phased picture lines 36 -> 211). The distinguisher is
 * that a genuine step PERSISTS while a bad pick does not, so instead of
 * rejecting a far result we take it slowly: search_win/4 samples on the
 * first line, doubling for each consecutive line whose result agrees on
 * the direction. An isolated outlier moves the line by 5 samples (2 px)
 * instead of 80 (30 px); a real step is still followed, over 3-4 lines.
 * A move within search_win is a normal correction and is taken whole.
 *
 * `dir_run`/`len_run` hold the run across lines; a shape lock resets
 * them (pass 0/0). Shared by scan_lines and LiveScan so the batch and
 * live paths cannot drift apart (cli/live-test.cpp checks they don't). */
inline long sync_slew(long phi, long target, const SyncParams &p,
                      int &dir_run, int &len_run)
{
    const long LINE = 4000;      /* one 120-rpm line, as everywhere else */
    long d = target - phi;
    if (d >  LINE / 2) d -= LINE;     /* take the short way round */
    if (d < -LINE / 2) d += LINE;
    long a = d < 0 ? -d : d;
    if (a > p.search_win) {
        int dir = d < 0 ? -1 : 1;
        if (dir == dir_run)
            len_run++;
        else {
            dir_run = dir;
            len_run = 1;
        }
        long step = p.search_win / 4 > 0 ? p.search_win / 4 : 1;
        long allow = step * (1L << (len_run > 20 ? 20 : len_run - 1));
        if (allow > a)
            allow = a;
        d = dir * allow;
    } else {
        dir_run = 0;
        len_run = 0;
    }
    return (phi + d + LINE) % LINE;
}

/* Scan the whole 8000 S/s video stream and extract pixel lines.
 * Lines are emitted from the very start (unrotated until lock, like
 * the original - the preamble is part of the image).
 * `line_state` (may be null) is called after each emitted line with
 * the line state (0 locked / 1 corrected / 2 coasting / 3 unlocked,
 * see above). For the GUI status LEDs; decode logic is unchanged.
 * `params` (may be null) overrides the default thresholds above.
 * `track_enable` = the Sync button (docs/01 sec. 3.2): when false, no
 * lock is attempted and the phase stays frozen at 0. */
FaxImage scan_lines(const std::vector<uint8_t> &video,
                    void (*line_state)(int state) = nullptr,
                    const SyncParams *params = nullptr,
                    bool track_enable = true);

#endif
