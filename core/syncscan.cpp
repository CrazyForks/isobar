/* syncscan.cpp - see syncscan.h. */

#include "syncscan.h"
#include <cstddef>   /* size_t — older libstdc++ doesn't leak it via <vector> */

namespace {

/* defaults (the values that reproduce the historical M0 behavior) */
const int DEF_MIN_PERIOD  = 3980;
const int DEF_MAX_PERIOD  = 4020;
const int DEF_MIN_PULSE   = 100;
const int DEF_MAX_PULSE   = 400;
const int DEF_SEARCH_WIN  = 40;
const int DEF_MAX_COAST   = 60;
const int DEF_DARK_TH     = 96;
const int DEF_LOCK_HYST   = 5;
const int DEF_FALLBACK_WIN = 160;   /* syn combo default: 20 ms @ 8 kHz */
const int DEF_FB_THRESH   = 10;     /* SyncThre combo index 0: 20*0+10  */

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
 * junk edges from image content in between (LReSycn lock hysteresis,
 * docs/01 sec. 3.2(8)). Returns edges.size() if none. */
size_t find_lock(const std::vector<long> &edges, size_t from,
                 const SyncParams &p)
{
    int need = p.lock_hyst > 0 ? p.lock_hyst : 1;
    for (size_t i = from; i + 1 < edges.size(); i++) {
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

/* Fallback sync tracking (docs/01 sec. 3.2(8)): search [lo, hi] for the
 * darkest position. Valid if it is dark (dark_th) and the dip below the
 * window mean is at least fb_thresh (SyncThre). The original's exact
 * validation is not in the spec; this is the documented approximation. */
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
    bool ever_locked = false; /* fallback search: full window until then */
    bool locked = false;      /* currently locked (release clears)      */
    long last_shape = -1;     /* absolute pos of last shape-locked edge */
    int miss = 0;             /* consecutive lines with no usable edge  */
    int since_shape = 0;      /* lines since the last shape-detected edge */
    size_t ei = 0;            /* cursor into edges (match window)       */
    size_t lock_from = 0;     /* cursor into edges (lock acquisition)   */

    for (long grid = 0; grid + LINE_SAMPLES <= total; grid += LINE_SAMPLES) {
        int how = 3;          /* 0 shape / 1 fallback / 2 coast / 3 unlocked */

        if (track_enable) {
            long expected = grid + phi;

            if (!locked) {
                /* lock acquisition: junk-tolerant chain of valid
                 * periods; the chain start must lie in this window,
                 * otherwise wait for the next line */
                size_t nl = find_lock(edges, lock_from, p);
                if (nl < edges.size() && edges[nl] < grid + LINE_SAMPLES) {
                    long E = edges[nl];
                    phi = (E - grid + LINE_SAMPLES) % LINE_SAMPLES;
                    last_shape = E;
                    if (ever_locked)
                        img.relocks++;   /* re-acquire after release */
                    locked = true;
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
                long fb = lo <= hi ? fallback_search(sm, lo, hi, p) : -1;
                if (fb >= 0) {
                    phi = (fb - grid + LINE_SAMPLES) % LINE_SAMPLES;
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
