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
 * what makes those lines unreadable.
 *
 * So: find the darkest fallback_win window in the WHOLE line and take it
 * only if it is unambiguous - the best rival at least 300 samples away
 * must be at least dark_th/2 brighter - and only once the PREVIOUS line
 * agreed on the same position. A real sync pulse wins by a mile and does
 * not move; picture content neither wins by a mile nor repeats. That
 * two-line confirmation is what makes a whole-line search safe here,
 * where the bare one used to re-lock onto dark picture content
 * (`phasing-test`, docs/01 sec. 3.2(8)).
 *
 * Returns the absolute position to snap to, or -1 to leave the phase
 * alone. `cand`/`cand_hits` carry the candidate across lines; a shape
 * lock resets them (pass -1/0). Shared by scan_lines and LiveScan. */
inline long sync_step_lock(const std::vector<int> &sm, long grid, long phi,
                           const SyncParams &p, long &cand, int &cand_hits)
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

    /* not a clear, dark pulse: forget any candidate we were building */
    if (m[best] >= p.dark_th || rival - m[best] < p.dark_th / 2) {
        cand = -1;
        cand_hits = 0;
        return -1;
    }
    long dc = best - cand < 0 ? cand - best : best - cand;
    if (cand >= 0 && (dc <= p.search_win || dc >= LINE - p.search_win))
        cand_hits++;
    else {
        cand = best;
        cand_hits = 1;
    }
    /* only worth acting on if it is somewhere the narrow search cannot
     * already reach - otherwise the ordinary tracking has it in hand */
    long dp = best - phi < 0 ? phi - best : best - phi;
    if (cand_hits < 2 || dp <= p.search_win || dp >= LINE - p.search_win)
        return -1;
    cand_hits = 0;
    return grid + best;
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
