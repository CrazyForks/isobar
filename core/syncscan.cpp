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
 * 1550 of 1851 lines on an off-air recording - measured, see docs/01. */
const int DEF_DARK_TH     = 96;     /* cf. Sync2Thre */
const int DEF_FB_THRESH   = 10;     /* ours: dip depth, second chance */
/* The original's SyncThre, and it takes the original's value: the ported
 * fallback below computes the same quantity the original does (a boxcar
 * mean over the binarised video), so unlike the two above, this number
 * transfers. Measured best on all three fixtures at exactly 30. */
const int DEF_FB_MEAN     = 30;     /* = SyncThre */

/* Ported fallback shape constants, compiled into the original with no
 * ini key (docs/01 sec. 3.2(8); kgfax.exe.c:5639-5640). */
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
 * length is a plausible sync pulse width (docs/01 sec. 3.2(7)). */
std::vector<long> find_sync_edges(const std::vector<int> &sm,
                                  const SyncParams &p)
{
    std::vector<long> edges;
    long run_start = -1;
    for (size_t i = 0; i < sm.size(); i++) {
        bool dark = sm[i] < p.dark_th;
        if (dark && run_start < 0)
            run_start = (long)i;
        else if (!dark && run_start >= 0) {
            long len = (long)i - run_start;
            if (len >= p.min_pulse && len <= p.max_pulse)
                edges.push_back(run_start);
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
size_t find_lock(const std::vector<long> &edges, size_t from,
                 long lo, long hi, long win_end, const SyncParams &p)
{
    int need = p.lock_hyst > 0 ? p.lock_hyst : 1;
    for (size_t i = from; i + 1 < edges.size(); i++) {
        if (edges[i] >= win_end)
            break;              /* chain start beyond this line window */
        if (edges[i] < lo || edges[i] > hi)
            continue;           /* outside the allowed neighbourhood */
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
 * anchor is the bright->dark edge - the same one our shape check
 * publishes. Same mechanism, one consistent reference. */
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
    std::vector<long> edges = find_sync_edges(sm, p);

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

    /* After this many consecutive unlocked lines, re-acquisition may
     * search a whole line again instead of only the neighbourhood of the
     * phase it was holding. A brief release mid-picture means noise, and
     * the old phase is still the best guess; a long dead stretch can mean
     * the transmission itself changed, and then the old phase is worthless
     * (the JMH sample does exactly this - a new transmission starts at
     * line 930 with the sync 2878 samples away, and it must be allowed to
     * follow). Three times the release counter: long enough that no
     * transient reaches it, short enough to catch a real handover. */
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
                size_t nl = find_lock(edges, lock_from, lo, hi,
                                      grid + LINE_SAMPLES, p);
                if (nl < edges.size()) {
                    long E = edges[nl];
                    phi = (E - grid) % LINE_SAMPLES;
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
            } else {
                /* shape match: candidate edge near the predicted edge.
                 * The period is checked against the PREVIOUS line's
                 * edge position (grid+phi), which stays ~4000 even
                 * across fallback/coast lines. */
                long prev_edge = grid - LINE_SAMPLES + phi;
                while (ei < edges.size() &&
                       edges[ei] < expected - p.search_win)
                    ei++;
                if (ei < edges.size() &&
                    edges[ei] <= expected + p.search_win) {
                    long E = edges[ei];
                    long per = E - prev_edge;
                    if (per >= p.min_period && per <= p.max_period) {
                        phi = (E - grid + LINE_SAMPLES) % LINE_SAMPLES;
                        fb_dir = 0;
                        fb_run = 0;
                        last_shape = E;
                        miss = 0;
                        since_shape = 0;
                        how = 0;
                    }
                }
            }

            if (how != 0) {
                /* fallback: min-brightness search (docs/01 sec. 3.2(8)).
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
                    if (fb < 0)
                        fb = fallback_search(sm, lo, hi, p);
                }
                if (fb >= 0) {
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
