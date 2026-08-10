/* syncscan.cpp - see syncscan.h. */

#include "syncscan.h"
#include <cstddef>   /* size_t — older libstdc++ doesn't leak it via <vector> */

namespace {

/* Defaults = the original program's own ini defaults (docs/01 sec. 6),
 * EXCEPT dark_th and fb_thresh: those two feed formulas that differ from
 * the original's, so its values do not transfer (DEVIATIONS.md #16).
 * min_period/max_period/min_pulse/max_pulse are hard-coded in the
 * original too, and have no ini key. */
const int DEF_MIN_PERIOD  = 3980;
const int DEF_MAX_PERIOD  = 4020;
const int DEF_MIN_PULSE   = 100;
const int DEF_MAX_PULSE   = 400;
const int DEF_SEARCH_WIN  = 20;     /* SyncWidth  */
const int DEF_MAX_COAST   = 10;     /* LReSycn    */
const int DEF_LOCK_HYST   = 5;      /* RReSycn    */
const int DEF_FALLBACK_WIN = 160;   /* syn combo default: 20 ms @ 8 kHz */
/* Ours, not the original's (Sync2Thre 20 / SyncThre 30): we threshold an
 * 8-sample moving average, the original binarises the raw video; and our
 * fb_thresh is a dip depth below the local mean where the original's is
 * an absolute bound on a boxcar mean. Its values in our formulas cost
 * 1550 of 1851 lines on an off-air recording - measured, see
 * DEVIATIONS.md #16. */
const int DEF_DARK_TH     = 96;     /* cf. Sync2Thre */
const int DEF_FB_THRESH   = 10;     /* ours: dip depth, second chance */
/* The original's SyncThre, and it takes the original's value: the ported
 * fallback below computes the same quantity the original does (a boxcar
 * mean over the binarised video), so unlike the two above, this number
 * transfers. Measured best on all three fixtures at exactly 30. */
const int DEF_FB_MEAN     = 30;     /* = SyncThre */

/* Ported fallback shape constants, hard-coded in the original with no
 * ini key (docs/01 sec. 3.2(8)). */
const int FB_GATE       = 8;        /* dword_4F25E4 */
const int FB_GATE_LEVEL = 128;      /* dword_4F25E8 */

const int LINE_SAMPLES = 4000;   /* one 120-rpm line = 0.5 s @ 8000 S/s */

/* 8-sample moving average: removes single-sample noise before the
 * dark-run shape check and the fallback brightness search. */
std::vector<int> moving_average(const std::vector<uint8_t> &v)
{
    size_t n = v.size();
    std::vector<int> sm(n);
    int acc = 0;
    for (size_t i = 0; i < n; i++) {
        acc += v[i];
        if (i >= 8)
            acc -= v[i - 8];
        sm[i] = acc / (i >= 7 ? 8 : (int)i + 1);
    }
    return sm;
}

/* Collect candidate sync edges: start index of every dark run whose
 * length is a plausible sync pulse width (docs/01 sec. 3.2(7)).
 * `dark_out` (may be null) receives each run's mean brightness, which
 * find_lock uses to tell a sync pulse from picture content. */
std::vector<long> find_sync_edges(const std::vector<int> &sm,
                                  const SyncParams &p,
                                  std::vector<int> *dark_out = nullptr)
{
    std::vector<long> edges;
    long run_start = -1;
    for (size_t i = 0; i < sm.size(); i++) {
        bool dark = sm[i] < p.dark_th;
        if (dark && run_start < 0)
            run_start = (long)i;
        else if (!dark && run_start >= 0) {
            long len = (long)i - run_start;
            if (len >= p.min_pulse && len <= p.max_pulse) {
                edges.push_back(run_start);
                if (dark_out) {
                    long s = 0;
                    for (long k = 0; k < len; k++)
                        s += sm[run_start + k];
                    dark_out->push_back((int)(s / len));
                }
            }
            run_start = -1;
        }
    }
    return edges;
}

/* Find the first index in edges (from `from`) that starts a valid lock:
 * a chain of lock_hyst valid periods (~4000 samples apart), skipping
 * junk edges from image content in between (lock_hyst, which comes from
 * RReSycn - NOT LReSycn, which is the release counter; docs/01
 * sec. 3.2(8)). Returns edges.size() if none. */
size_t find_lock(const std::vector<long> &edges, const std::vector<int> &darks,
                 size_t from, long lo, long hi, long win_end,
                 const SyncParams &p)
{
    int need = p.lock_hyst > 0 ? p.lock_hyst : 1;
    for (size_t i = from; i + 1 < edges.size(); i++) {
        if (edges[i] >= win_end)
            break;              /* chain start beyond this line window */
        if (edges[i] < lo || edges[i] > hi)
            continue;           /* outside the allowed neighbourhood */
        if (i < darks.size() && darks[i] >= sync_dark_floor(p))
            continue;           /* too pale to be a sync pulse */
        long cur = edges[i];
        int links = 0;
        for (size_t j = i + 1; j < edges.size(); j++) {
            long per = edges[j] - cur;
            if (per > p.max_period)
                break;          /* chain broken: gap too big */
            if (per < p.min_period)
                continue;       /* junk edge from image content: skip */
            cur = edges[j];
            if (++links >= need)
                return i;
        }
    }
    return edges.size();
}

/* Fallback sync tracking: search [lo, hi] for the darkest position.
 * Valid if it is dark (dark_th) and the dip below the window mean is at
 * least fb_thresh. NOTE this is deliberately not the original's test -
 * it slides a boxcar over the binarised video and accepts the minimum
 * MEAN if that mean is below SyncThre (docs/01 sec. 3.2(8)). Ours is a
 * dip depth relative to the local mean, which is why fb_thresh does not
 * take SyncThre's value (DEVIATIONS.md #16). */
long fallback_search(const std::vector<int> &sm, long lo, long hi,
                     const SyncParams &p)
{
    long n = (long)sm.size();
    if (lo < 0) lo = 0;
    if (hi >= n) hi = n - 1;
    if (lo >= hi)
        return -1;

    long best = lo;
    long sum = 0;
    for (long i = lo; i <= hi; i++) {
        sum += sm[i];
        if (sm[i] < sm[best])
            best = i;
    }
    long mean = sum / (hi - lo + 1);
    if (sm[best] < p.dark_th && mean - sm[best] >= p.fb_thresh)
        return best;
    return -1;
}

/* The original's fallback tracker, docs/01 sec. 3.2(8), in OUR reference
 * convention. It slides a boxcar of fallback_win samples over the
 * binarised video and keeps the position with the minimum mean, but only
 * where the samples just outside the window are bright - i.e. the window
 * is a dark run with a bright edge, not merely a dark patch. Valid when
 * that minimum mean is below fb_mean, an absolute bound (SyncThre), and
 * when the move from the previous position is within search_win
 * (MaxJump) or is a wrap-around the long way.
 *
 * One deliberate departure: the original gates on the samples AFTER the
 * window and publishes the window's start, so its fallback anchors the
 * dark->bright edge while its own shape check anchors the bright pulse -
 * two reference points a whole fallback_win apart, which its own jump
 * guard then rejects (docs/01 sec. 3.2(8) "pick one reference point").
 * We gate on the samples BEFORE the window and publish its start, so the
 * raw position is the bright->dark edge - the same raw reference our
 * shape check starts from; sync_anchor() then refines both to the
 * pulse's darkest-window centre, so the phase is published at one
 * consistent reference. Same mechanism, one consistent reference. */
long fallback_edge(const std::vector<int> &sm, long lo, long hi,
                   const SyncParams &p, long prev, bool ever_locked)
{
    long n = (long)sm.size();
    int win = p.fallback_win > 0 ? p.fallback_win : 160;
    if (lo < FB_GATE) lo = FB_GATE;
    if (hi > n - win) hi = n - win;
    if (lo > hi)
        return -1;

    long best = -1;
    int best_mean = 256;
    for (long q = lo; q <= hi; q++) {
        int gate = 0;
        for (int i = 1; i <= FB_GATE; i++)
            gate += sm[q - i] >= p.dark_th ? 255 : 0;
        if (gate / FB_GATE <= FB_GATE_LEVEL)
            continue;            /* no bright->dark edge here */
        int mean = 0;
        for (int i = 0; i < win; i++)
            mean += sm[q + i] >= p.dark_th ? 255 : 0;
        mean /= win;
        if (mean < best_mean) {
            best_mean = mean;
            best = q;
        }
    }
    if (best < 0 || best_mean >= p.fb_mean)
        return -1;
    if (ever_locked) {
        long d = best - prev < 0 ? prev - best : best - prev;
        if (d > p.search_win && d < LINE_SAMPLES - p.search_win)
            return -1;           /* jumped further than MaxJump */
    }
    return best;
}

} /* namespace */

namespace {

/* Inverted (WMO phasing) edge collection, DEVIATIONS.md #19: mirror of
 * find_sync_edges with the polarity flipped. A candidate is a BRIGHT run
 * whose length is a plausible phasing pulse width; the published position
 * is the run's END (first non-bright sample), not its start - one fixed
 * reference for every caller, exactly as find_sync_edges publishes the
 * dark run's start. SYNC_INV_OFFSET then carries it to the line's first
 * pixel (which is the pulse's LEADING edge - see the constant).
 * `bright_out` receives each run's mean brightness. */
std::vector<long> find_inv_edges(const std::vector<int> &sm,
                                 const SyncParams &p,
                                 std::vector<int> *bright_out = nullptr)
{
    std::vector<long> edges;
    long run_start = -1;
    for (size_t i = 0; i < sm.size(); i++) {
        bool bright = sm[i] >= sync_bright_floor(p);
        if (bright && run_start < 0)
            run_start = (long)i;
        else if (!bright && run_start >= 0) {
            long len = (long)i - run_start;
            if (len >= p.min_pulse && len <= p.max_pulse) {
                edges.push_back((long)i);
                if (bright_out) {
                    long s = 0;
                    for (long k = run_start; k < (long)i; k++)
                        s += sm[k];
                    bright_out->push_back((int)(s / len));
                }
            }
            run_start = -1;
        }
    }
    return edges;
}

/* Inverted lock acquisition, DEVIATIONS.md #19: mirror of find_lock for
 * the white phasing pulse. Same junk-tolerant chain of lock_hyst valid
 * periods; differences: the chain start's line must be dark-dominant
 * (sync_line_dark - phasing, not picture) and the run must be genuinely
 * white (bright floor, the sync_dark_floor mirror). Returns inv_edges.size()
 * if none. `grid` is the current line window, for the line-dark gate. */
size_t find_inv_lock(const std::vector<long> &inv_edges,
                     const std::vector<int> &inv_bright,
                     size_t from, long lo, long hi, long win_end,
                     const std::vector<int> &sm, long grid,
                     const SyncParams &p)
{
    int need = p.lock_hyst > 0 ? p.lock_hyst : 1;
    for (size_t i = from; i + 1 < inv_edges.size(); i++) {
        if (inv_edges[i] >= win_end)
            break;              /* chain start beyond this line window */
        if (inv_edges[i] < lo || inv_edges[i] > hi)
            continue;           /* outside the allowed neighbourhood */
        if (i < inv_bright.size() && inv_bright[i] < sync_bright_floor(p))
            continue;           /* too pale to be a phasing pulse */
        if (!sync_line_dark(sm, grid, p))
            return inv_edges.size();   /* not phasing: do not look further */
        long cur = inv_edges[i];
        int links = 0;
        for (size_t j = i + 1; j < inv_edges.size(); j++) {
            long per = inv_edges[j] - cur;
            if (per > p.max_period)
                break;          /* chain broken: gap too big */
            if (per < p.min_period)
                continue;       /* junk edge: skip */
            cur = inv_edges[j];
            if (++links >= need)
                return i;
        }
    }
    return inv_edges.size();
}

} /* namespace */

SyncParams sync_default_params()
{
    SyncParams p;
    p.min_period   = DEF_MIN_PERIOD;
    p.max_period   = DEF_MAX_PERIOD;
    p.min_pulse    = DEF_MIN_PULSE;
    p.max_pulse    = DEF_MAX_PULSE;
    p.search_win   = DEF_SEARCH_WIN;
    p.max_coast    = DEF_MAX_COAST;
    p.dark_th      = DEF_DARK_TH;
    p.lock_hyst    = DEF_LOCK_HYST;
    p.fallback_win = DEF_FALLBACK_WIN;
    p.fb_thresh    = DEF_FB_THRESH;
    p.fb_mean      = DEF_FB_MEAN;
    return p;
}

FaxImage scan_lines(const std::vector<uint8_t> &video,
                    void (*line_state)(int),
                    const SyncParams *params,
                    bool track_enable)
{
    const SyncParams p = params ? *params : sync_default_params();

    FaxImage img;
    img.lines_locked = 0;
    img.lines_corrected = 0;
    img.lines_coasted = 0;
    img.relocks = 0;

    long total = (long)video.size();
    if (total < LINE_SAMPLES)
        return img;

    std::vector<int> sm = moving_average(video);
    std::vector<int> edge_dark;
    std::vector<long> edges = find_sync_edges(sm, p, &edge_dark);
    /* WMO inverted phasing (DEVIATIONS.md #19): white-pulse candidates */
    std::vector<int> inv_bright;
    std::vector<long> inv_edges = find_inv_edges(sm, p, &inv_bright);

    /* Line grid (docs/01 sec. 3.2): line n covers samples
     * [n*4000, (n+1)*4000), emitted rotated by phi so the sync edge
     * lands at index 0. Sync tracking only adjusts phi; the grid never
     * moves. Lines are emitted from the very start (state 3 until lock),
     * like the original - the sync/phasing preamble is part of the image. */
    long phi = 0;             /* rotation offset within the line window */
    int fb_dir = 0;           /* direction of the current far-fallback run */
    int fb_run = 0;           /* consecutive far results agreeing on it    */
    bool ever_locked = false; /* fallback search: full window until then */
    bool locked = false;      /* currently locked (release clears)      */
    long last_shape = -1;     /* absolute pos of last shape-locked edge */
    int miss = 0;             /* consecutive lines with no usable edge  */
    int since_shape = 0;      /* lines since the last shape-detected edge */
    size_t ei = 0;            /* cursor into edges (match window)       */
    size_t lock_from = 0;     /* cursor into edges (lock acquisition)   */
    int unlocked_for = 0;     /* consecutive lines with no lock         */
    bool inv_mode = false;    /* holding a WMO-phasing phase (DEV #19)  */
    size_t inv_ei = 0;        /* cursor into inv_edges (match window)   */
    size_t inv_lock_from = 0; /* cursor into inv_edges (acquisition)    */
    long esc_prev = -1;       /* last full-window escape chain's phase  */
    int esc_run = 0;          /* consecutive lines agreeing on it       */
    int inv_dark_run = 0;     /* consecutive black-dominant lines       */
    SyncInvDrift inv_drift;   /* line period fitted to the preamble     */
    double inv_period = 4000.0;
    double inv_phase = 0.0;   /* exact coasting phase (fractional)      */
    inv_drift.reset();

    /* After this many consecutive unlocked lines, re-acquisition may
     * search a whole line again instead of only the neighbourhood of the
     * phase it was holding. A brief release mid-picture means noise, and
     * the old phase is still the best guess; a long dead stretch can mean
     * the transmission itself changed, and then the old phase is worthless
     * (the JMH sample does exactly this - a new transmission starts at
     * line 930 with the sync 2878 samples away, and it must be allowed to
     * follow). Three times the release counter: long enough that no
     * transient reaches it, short enough to catch a real handover.
     *
     * In practice this is now a SAFETY NET rather than the working escape
     * route, and sessions 27-28 were wrong to worry that it never fires.
     * Measured (S29, cli/reacq-test.cpp): the threshold is reached on 49
     * lines of the full himawari reception and 27 of jmh-phasing-16k, so it
     * is reachable - but disabling it entirely changes no test and no
     * measured number on any fixture, because sync_step_lock gets there
     * first. Against an injected step of 200..2000 samples, with or without
     * a dead stretch before it, step-lock restores the phase on the line the
     * step happens; with step-lock removed the same step leaves 35 of 74
     * lines mis-phased even with this widen available, and with both removed
     * all 74. Kept because it is the only way back when sync_line_pulse
     * cannot find an unambiguous pulse at all, which no fixture covers. */
    const int widen_after = p.max_coast > 0 ? 3 * p.max_coast : 30;

    for (long grid = 0; grid + LINE_SAMPLES <= total; grid += LINE_SAMPLES) {
        int how = 3;          /* 0 shape / 1 fallback / 2 coast / 3 unlocked */

        if (track_enable) {
            long expected = grid + phi;
            if (!locked)
                unlocked_for++;
            else
                unlocked_for = 0;

            if (!locked) {
                /* lock acquisition: junk-tolerant chain of valid
                 * periods; the chain start must lie in this window,
                 * otherwise wait for the next line.
                 * Skip edges before the window first: after a release
                 * lock_from sits at the last shape edge, which may be
                 * several lines back, and an edge from an earlier line
                 * would give a phase that does not describe this one. */
                while (lock_from < edges.size() && edges[lock_from] < grid)
                    lock_from++;
                /* Re-acquisition after a release may only land near the
                 * phase we already had: a transmission's sync position
                 * does not move, only its quality does. Without this,
                 * a preamble of all-dark lines (no dark RUN, so no edge)
                 * releases the lock, and the full-window search then
                 * re-locks onto a dark feature in the picture - shifting
                 * the image sideways for tens of lines. Same
                 * neighbourhood the fallback search uses. The first lock
                 * of the stream still searches the whole line, and so
                 * does re-acquisition once the decoder has gone
                 * widen_after lines with no lock at all. */
                long lo = grid, hi = grid + LINE_SAMPLES - 1;
                if (ever_locked && p.fallback_win > 0 &&
                    unlocked_for < widen_after) {
                    if (expected - p.fallback_win / 2 > lo)
                        lo = expected - p.fallback_win / 2;
                    if (expected + p.fallback_win / 2 < hi)
                        hi = expected + p.fallback_win / 2;
                }
                size_t nl = find_lock(edges, edge_dark, lock_from, lo, hi,
                                      grid + LINE_SAMPLES, p);
                if (nl < edges.size()) {
                    long E = edges[nl];
                    phi = (sync_anchor(sm, E, p) - grid + LINE_SAMPLES)
                          % LINE_SAMPLES;
                    fb_dir = 0;
                    fb_run = 0;
                    last_shape = E;
                    if (ever_locked)
                        img.relocks++;   /* re-acquire after release */
                    locked = true;
                    unlocked_for = 0;
                    ever_locked = true;
                    miss = 0;
                    since_shape = 0;
                    how = 0;
                    ei = nl + 1;
                    lock_from = nl + 1;
                }
                if (how != 0 && !ever_locked) {
                    /* WMO inverted phasing (DEVIATIONS.md #19): a chain
                     * of white pulses on dark lines, in the same window.
                     * The anchor is the pulse's trailing edge plus
                     * SYNC_INV_OFFSET (i.e. its leading edge).
                     * Only before the first lock of the stream: once ANY
                     * phase reference exists, the established convention
                     * owns it - a JMH-style preamble is the same white-
                     * pulse-in-black shape, but its picture sync sits
                     * elsewhere, and inv-locking it would throw away a
                     * phase that was already right (phasing-test). */
                    while (inv_lock_from < inv_edges.size() &&
                           inv_edges[inv_lock_from] < grid)
                        inv_lock_from++;
                    size_t nil = find_inv_lock(inv_edges, inv_bright,
                                               inv_lock_from, lo, hi,
                                               grid + LINE_SAMPLES, sm,
                                               grid, p);
                    if (nil < inv_edges.size()) {
                        long E = inv_edges[nil];
                        phi = (E + SYNC_INV_OFFSET - grid + LINE_SAMPLES)
                              % LINE_SAMPLES;
                        /* start measuring the line period here: this is
                         * the first anchor of the preamble that will be
                         * coasted on (DEVIATIONS.md #19) */
                        inv_drift.reset();
                        inv_drift.add(grid / LINE_SAMPLES,
                                      E + SYNC_INV_OFFSET);
                        inv_period = 4000.0;
                        inv_phase = (double)phi;
                        inv_dark_run = SYNC_PHASING_CONFIRM;
                        fb_dir = 0;
                        fb_run = 0;
                        last_shape = E;
                        if (ever_locked)
                            img.relocks++;
                        locked = true;
                        inv_mode = true;
                        unlocked_for = 0;
                        ever_locked = true;
                        miss = 0;
                        since_shape = 0;
                        how = 0;
                        inv_ei = nil + 1;
                        inv_lock_from = nil + 1;
                        esc_prev = -1;
                        esc_run = 0;
                    }
                }
            } else if (inv_mode) {
                /* Holding a phase acquired from WMO phasing
                 * (DEVIATIONS.md #19). The picture carries no pulse to
                 * track, so there is no fallback and no release in this
                 * mode - coasting is exactly right. Two things may still
                 * move or take over the phase: */
                long bracket = p.search_win +
                               (p.fallback_win > 0 ? p.fallback_win : 160) / 2;
                /* a black-pulse chain means a JMH-style station took over
                 * the frequency. Searched narrow first (its own phasing
                 * will already have re-anchored us below, so its picture
                 * pulses land nearby); if nothing is there, the whole
                 * line - JMH's picture sync sits ~260 samples from its
                 * phasing anchor (measured on "jmh sample.wav"), far
                 * outside the narrow window. A full-window search on every
                 * line would re-expose picture content to false chain
                 * locks, so the far result must CONFIRM: a real station's
                 * pulses chain at the same phase on consecutive lines,
                 * picture content does not (the sync_step_lock
                 * philosophy). The takeover happens after
                 * SYNC_ESC_CONFIRM agreeing lines (a tall chart feature
                 * chains too - 12 lines measured - but not for long).
                 * The whole escape is skipped on black-dominant lines: a
                 * real takeover's picture lines are not dark, but VMW's
                 * own end-of-chart band is - and its dotted ruler chains
                 * and confirms exactly like a sync pulse (measured: a
                 * false escape 396 px off at line ~1240 of the VMW
                 * recording). */
                /* black-dominant lines in a row: a phasing preamble is at
                 * least 60 of them, a chart's dark band is a handful
                 * (SYNC_PHASING_CONFIRM) */
                bool dark = sync_line_dark(sm, grid, p);
                if (dark)
                    inv_dark_run++;
                else
                    inv_dark_run = 0;

                size_t nl = edges.size();
                if (!dark) {
                while (lock_from < edges.size() && edges[lock_from] < grid)
                    lock_from++;
                long elo = grid, ehi = grid + LINE_SAMPLES - 1;
                if (p.fallback_win > 0) {
                    if (expected - p.fallback_win / 2 > elo)
                        elo = expected - p.fallback_win / 2;
                    if (expected + p.fallback_win / 2 < ehi)
                        ehi = expected + p.fallback_win / 2;
                }
                nl = find_lock(edges, edge_dark, lock_from, elo, ehi,
                               grid + LINE_SAMPLES, p);
                if (nl == edges.size()) {
                    nl = find_lock(edges, edge_dark, lock_from, grid,
                                   grid + LINE_SAMPLES - 1,
                                   grid + LINE_SAMPLES, p);
                    if (nl < edges.size()) {
                        long f = (edges[nl] - grid + LINE_SAMPLES)
                                 % LINE_SAMPLES;
                        bool same = false;
                        if (esc_prev >= 0) {
                            long d = f - esc_prev;
                            if (d < 0) d = -d;
                            if (d > LINE_SAMPLES / 2) d = LINE_SAMPLES - d;
                            same = d <= p.search_win;
                        }
                        if (same)
                            esc_run++;
                        else {
                            esc_prev = f;
                            esc_run = 1;
                        }
                        if (esc_run < SYNC_ESC_CONFIRM)
                            nl = edges.size();   /* not proven yet */
                    }
                    /* no chain on this line: esc_prev/esc_run are kept -
                     * a pulse embedded in photo content is missed on some
                     * lines, and resetting would never reach the count */
                }
                }
                if (nl < edges.size()) {
                    long E = edges[nl];
                    phi = (sync_anchor(sm, E, p) - grid + LINE_SAMPLES)
                          % LINE_SAMPLES;
                    fb_dir = 0;
                    fb_run = 0;
                    last_shape = E;
                    img.relocks++;
                    locked = true;
                    inv_mode = false;
                    unlocked_for = 0;
                    miss = 0;
                    since_shape = 0;
                    how = 0;
                    ei = nl + 1;
                    lock_from = nl + 1;
                } else if (dark && inv_dark_run >= SYNC_PHASING_CONFIRM) {
                    /* still (or again) phasing: re-anchor on this line's
                     * pulse, so the next chart's preamble re-seats the
                     * phase and keeps the period measurement going. The
                     * run-length gate is what keeps a chart's own dark
                     * bands from re-anchoring mid-picture. */
                    if (inv_dark_run == SYNC_PHASING_CONFIRM &&
                        inv_drift.have && grid / LINE_SAMPLES - inv_drift.first
                                          > SYNC_DRIFT_MIN_PTS)
                        inv_drift.reset();   /* a NEW preamble: measure it
                                              * on its own, keeping the
                                              * estimate in hand until the
                                              * fit is trustworthy again */
                    long prev_edge = grid - LINE_SAMPLES + phi;
                    /* bracket the RAW edges, which sit SYNC_INV_OFFSET away
                     * from the anchor the prediction is in - the anchor
                     * itself is tested below, as the shape match does */
                    long ecen = expected - SYNC_INV_OFFSET;
                    while (inv_ei < inv_edges.size() &&
                           inv_edges[inv_ei] < ecen - bracket)
                        inv_ei++;
                    for (size_t k = inv_ei;
                         k < inv_edges.size() &&
                         inv_edges[k] <= ecen + bracket; k++) {
                        /* the anchor is the trailing edge plus the
                         * offset, everywhere (SYNC_INV_OFFSET) */
                        long A = inv_edges[k] + SYNC_INV_OFFSET;
                        if (A < expected - p.search_win ||
                            A > expected + p.search_win)
                            continue;
                        /* prev_edge carries the offset through phi, so
                         * the period is judged anchor-to-anchor */
                        long per = A - prev_edge;
                        if (per >= p.min_period && per <= p.max_period) {
                            phi = (A - grid + LINE_SAMPLES) % LINE_SAMPLES;
                            /* every preamble anchor feeds the period fit;
                             * the estimate itself is only replaced once
                             * the fit has enough of them */
                            inv_drift.add(grid / LINE_SAMPLES, A);
                            if (inv_drift.n >= (double)SYNC_DRIFT_MIN_PTS)
                                inv_period = inv_drift.period();
                            inv_phase = (double)phi;
                            fb_dir = 0;
                            fb_run = 0;
                            last_shape = inv_edges[k];
                            miss = 0;
                            since_shape = 0;
                            how = 0;
                        }
                        break;
                    }
                }
                if (how != 0) {
                    /* Hold the phase - at the MEASURED line period, not at
                     * the nominal 4000 (DEVIATIONS.md #19). This is the
                     * whole difference between a chart that stands square
                     * and one sheared by the receiver's clock: nothing in
                     * the picture can correct the phase here, so the rate
                     * measured off the preamble is all there is. */
                    inv_phase += inv_period - (double)LINE_SAMPLES;
                    if (inv_phase < 0.0)
                        inv_phase += (double)LINE_SAMPLES;
                    else if (inv_phase >= (double)LINE_SAMPLES)
                        inv_phase -= (double)LINE_SAMPLES;
                    phi = (long)(inv_phase + 0.5) % LINE_SAMPLES;
                    /* ... and follow a dropout the same way, off the
                     * picture's own content: the rate is right but the
                     * stream can still lose samples, and here nothing
                     * else would ever notice (sync_content_step) */
                    if (!dark) {
                        long lag = sync_content_step(
                            sm, grid, phi,
                            grid + 2 * LINE_SAMPLES <= total);
                        if (lag != 0) {
                            inv_phase += (double)lag;
                            while (inv_phase < 0.0)
                                inv_phase += (double)LINE_SAMPLES;
                            while (inv_phase >= (double)LINE_SAMPLES)
                                inv_phase -= (double)LINE_SAMPLES;
                            phi = (long)(inv_phase + 0.5) % LINE_SAMPLES;
                            img.relocks++;
                        }
                    }
                    miss++;
                    since_shape++;
                    how = 2;
                }
            } else {
                /* shape match: candidate edge near the predicted edge.
                 * The period is checked against the PREVIOUS line's
                 * edge position (grid+phi), which stays ~4000 even
                 * across fallback/coast lines. */
                long prev_edge = grid - LINE_SAMPLES + phi;
                /* The window is judged on the ANCHOR, not on the raw edge
                 * the detector published: the prediction is an anchor, and
                 * comparing it against a leading edge shifts the +-search_win
                 * window by the gap between the two - which cost 43 shape
                 * locks and tripled the relocks on the himawari recording
                 * when the anchor went in. So bracket generously on raw
                 * position (an anchor sits within win/2 of its own edge) and
                 * test the anchor itself. */
                long bracket = p.search_win +
                               (p.fallback_win > 0 ? p.fallback_win : 160) / 2;
                while (ei < edges.size() && edges[ei] < expected - bracket)
                    ei++;
                for (size_t k = ei;
                     k < edges.size() && edges[k] <= expected + bracket; k++) {
                    long E = sync_anchor(sm, edges[k], p);
                    if (E < expected - p.search_win ||
                        E > expected + p.search_win)
                        continue;
                    long per = E - prev_edge;
                    if (per >= p.min_period && per <= p.max_period) {
                        phi = (E - grid + LINE_SAMPLES) % LINE_SAMPLES;
                        fb_dir = 0;
                        fb_run = 0;
                        last_shape = edges[k];   /* raw: indexes `edges` */
                        miss = 0;
                        since_shape = 0;
                        how = 0;
                    }
                    break;
                }
            }

            if (how != 0 && !inv_mode) {
                /* fallback: min-brightness search (docs/01 sec. 3.2(8)).
                 * (Skipped in inv_mode: a WMO-phasing picture has no
                 * pulse to find - holding the phase is correct there.)
                 * Full window until first lock, narrow window after. */
                long lo, hi;
                if (ever_locked) {
                    if (p.fallback_win > 0) {
                        lo = expected - p.fallback_win / 2;
                        hi = expected + p.fallback_win / 2;
                    } else {
                        lo = 0;
                        hi = -1;   /* fallback disabled */
                    }
                } else {
                    lo = grid;
                    hi = grid + LINE_SAMPLES - 1;
                }
                /* The ported tracker first: it is the more selective of
                 * the two, and where it fires it is right. Where it
                 * declines, our own dip-depth search is a second chance
                 * - better than coasting, measured on all three
                 * fixtures (the ported test alone coasts through 45
                 * picture lines of jmh-phasing-16k that ours corrects). */
                long fb = -1;
                bool snapped = false;
                /* A genuine step in the sync position is further than
                 * either narrow search can reach, so look at the whole
                 * line first - sync_step_lock only answers when the
                 * pulse is unambiguous and the previous line agreed. */
                if (ever_locked) {
                    fb = sync_step_lock(sm, grid, phi, p);
                    snapped = fb >= 0;
                }
                if (fb < 0 && lo <= hi) {
                    fb = fallback_edge(sm, lo, hi, p, expected, ever_locked);
                    bool ported = fb >= 0;
                    if (fb < 0)
                        fb = fallback_search(sm, lo, hi, p);
                    /* Hold rather than slew onto dark picture. A missed
                     * shape check does not mean the phase is wrong - it
                     * usually means this one line's pulse edge merged or
                     * vanished (3% of lines on himawari). Slewing toward
                     * whatever is darkest within +-fallback_win/2 then
                     * CORRUPTS a phase that was correct, and the next
                     * line's prediction inherits the error: 727 of 2003
                     * lines arrived at the shape check already 41-200
                     * samples out. Coasting keeps the good phase and lets
                     * the next line's shape check resume.
                     * Same test as the chain-start floor, for the same
                     * reason - a real pulse is absolutely black, dark
                     * picture is not.
                     * Only OUR dip-depth second chance is gated. The
                     * ported tracker above already carries the original's
                     * own bound (fb_mean/SyncThre) and earns its answers;
                     * gating it too rejected genuine degraded pulses on
                     * clean off-air audio and emptied the fallback path
                     * that offair-test exists to cover. */
                    if (!ported && fb >= 0 &&
                        sync_run_mean(sm, fb, p) >= sync_dark_floor(p))
                        fb = -1;
                }
                if (fb >= 0) {
                    /* one reference for every path (sync_anchor) - a
                     * fallback result published at the run's leading edge
                     * and a shape lock published at the darkest window
                     * would otherwise tear the strip whenever tracking
                     * switched between them */
                    fb = sync_anchor(sm, fb, p);
                    long tgt = (fb - grid + LINE_SAMPLES) % LINE_SAMPLES;
                    /* a confirmed step is taken whole; anything else is
                     * rate-limited (see sync_slew) */
                    if (snapped) {
                        phi = tgt;
                        since_shape = 0;   /* confirmed, so not a miss */
                    } else {
                        phi = sync_slew(phi, tgt, p, fb_dir, fb_run);
                    }
                    how = 1;
                    miss = 0;
                } else {
                    /* hold phi (coast) */
                    miss++;
                    how = 2;
                }
                since_shape++;
                /* full release (RReSycn): too long without a shape
                 * edge - re-acquire sync on the next lines */
                if (locked && since_shape > p.max_coast) {
                    locked = false;
                    /* re-acquisition may start at the next plausible
                     * edge after the last known position */
                    while (lock_from < edges.size() &&
                           edges[lock_from] <= last_shape)
                        lock_from++;
                }
            }
        }

        /* LED/state: tolerate brief misses before showing "coasting"
         * (LReSycn release hysteresis); never-locked lines report
         * state 3 */
        int state = how;
        if (how == 2) {
            if (!locked)
                state = 3;
            else if (miss <= p.lock_hyst)
                state = 0;
        }

        /* emit the rotated line: out[i] = video[grid + (i+phi) % 4000] */
        std::vector<uint8_t> px(FaxImage::WIDTH);
        for (int i = 0; i < FaxImage::WIDTH; i++)
            px[i] = video[grid + ((8L * i) / 3 + phi) % LINE_SAMPLES];
        img.lines.push_back(px);
        if (how == 0)
            img.lines_locked++;
        else if (how == 1)
            img.lines_corrected++;
        else
            img.lines_coasted++;
        if (line_state)
            line_state(state);
    }

    return img;
}
