/* live.cpp - see live.h. Algorithms mirror syncscan.cpp; read that
 * file's comments for the spec mapping. Only the streaming
 * re-structuring is commented here. */

#include "live.h"

namespace {
const int LINE_SAMPLES = 4000;   /* one 120-rpm line = 0.5 s @ 8000 S/s */
}

LiveScan::LiveScan(const SyncParams &params)
    : p(params), track(true), man_delta(0), man_req(false),
      fin_req(false), fin_done(false)
{
    reset();
}

void LiveScan::reset()
{
    buf.clear();
    sm.clear();
    acc = 0;
    edges.clear();
    edge_dark.clear();
    run_start = -1;
    inv_edges.clear();
    inv_bright.clear();
    inv_run_start = -1;
    inv_mode = false;
    inv_lock_from = 0;
    esc_prev = -1;
    esc_run = 0;
    inv_dark_run = 0;
    inv_drift.reset();
    inv_period = 4000.0;
    inv_phase = 0.0;
    phi = 0;
    fb_dir = 0;
    fb_run = 0;
    finishing = false;
    manual_hold = false;
    ever_locked = false;
    locked = false;
    last_shape = -1;
    miss = 0;
    since_shape = 0;
    unlocked_for = 0;
    counted_grid = -1;
    lock_from = 0;
    track_applied = true;
    man_req.store(false);
    fin_req.store(false);
    fin_done.store(false);
    lines_locked = 0;
    lines_corrected = 0;
    lines_coasted = 0;
    relocks = 0;
}

void LiveScan::request_finish()
{
    /* just publish; feed() (audio thread) performs it. fin_done first,
     * so a caller polling it cannot see a stale "already done". */
    fin_done.store(false);
    fin_req.store(true);
}

void LiveScan::finish(void (*line_cb)(const uint8_t *, int, void *),
                      void *ud)
{
    /* End of stream: emit whatever pump() is holding back for lookahead.
     * NOT thread-safe - call it on the same thread that calls feed(). */
    finishing = true;
    pump(line_cb, ud);
    finishing = false;
}

void LiveScan::set_track(bool on)
{
    /* just publish; pump() (audio thread) applies the transition */
    track.store(on);
}

void LiveScan::nudge_phase(long delta)
{
    /* just publish; pump() (audio thread) applies it. Tracking comes
     * back on with it - the original's click presses the Sync button.
     * man_req last: it is what pump() tests, so delta is already there
     * by the time the request is visible. */
    man_delta.store(delta);
    track.store(true);
    man_req.store(true);
}

int LiveScan::lines_emitted() const
{
    return lines_locked + lines_corrected + lines_coasted;
}

void LiveScan::feed(const uint8_t *data, size_t n,
                    void (*line_cb)(const uint8_t *, int, void *), void *ud)
{
    for (size_t k = 0; k < n; k++) {
        size_t i = buf.size();
        uint8_t v = data[k];
        buf.push_back(v);

        /* moving average, window 8 (same as syncscan) */
        acc += v;
        if (i >= 8)
            acc -= buf[i - 8];
        int m = acc / (i >= 7 ? 8 : (int)i + 1);
        sm.push_back(m);

        /* dark-run shape check: run end -> candidate edge */
        bool dark = m < p.dark_th;
        if (dark && run_start < 0)
            run_start = (long)i;
        else if (!dark && run_start >= 0) {
            long len = (long)i - run_start;
            if (len >= p.min_pulse && len <= p.max_pulse) {
                edges.push_back(run_start);
                long s = 0;      /* `j`, not `k`: the feed() loop above owns
                                  * that name, and MSVC warns (C4456) */
                for (long j = 0; j < len; j++)
                    s += sm[run_start + j];
                edge_dark.push_back((int)(s / len));
            }
            run_start = -1;
        }

        /* bright-run (WMO phasing pulse) candidate, DEVIATIONS.md #19:
         * mirrors the dark-run check above with the polarity flipped;
         * the position published is the run's END; SYNC_INV_OFFSET
         * carries it to the line start (syncscan.cpp's twin) */
        bool bright = m >= sync_bright_floor(p);
        if (bright && inv_run_start < 0)
            inv_run_start = (long)i;
        else if (!bright && inv_run_start >= 0) {
            long len = (long)i - inv_run_start;
            if (len >= p.min_pulse && len <= p.max_pulse) {
                inv_edges.push_back((long)i);
                long s = 0;
                for (long j = inv_run_start; j < (long)i; j++)
                    s += sm[j];
                inv_bright.push_back((int)(s / len));
            }
            inv_run_start = -1;
        }
    }
    pump(line_cb, ud);

    /* a flush posted by another thread runs here, on this one */
    if (fin_req.exchange(false)) {
        finishing = true;
        pump(line_cb, ud);
        finishing = false;
        fin_done.store(true);
    }
}

/* Lock acquisition (syncscan's find_lock, streaming version): first
 * edge (>= lock_from) starting a chain of lock_hyst valid periods,
 * junk edges skipped.
 * Returns the chain-start edge position when it lies inside the
 * current window [grid, grid+4000);
 * -1 = undecidable yet (wait for more data);
 * -2 = no chain starts in this window (emit unlocked, retry next line).
 * An edge start is knowable only max_pulse samples after the fact, so
 * the horizon is buf.size() - max_pulse - 1. */
long LiveScan::try_lock(long grid, bool full_window)
{
    long finalized = (long)buf.size() - p.max_pulse - 1;
    int need = p.lock_hyst > 0 ? p.lock_hyst : 1;

    /* Allowed neighbourhood for the chain start; mirrors syncscan's
     * find_lock (the two must stay byte-identical - live-test). Before
     * the first lock the whole line is searched; afterwards only the
     * fallback's neighbourhood around the expected phase, so an
     * all-dark preamble cannot make the phase run away. The search
     * widens again after widen_after lines with no lock at all, so a
     * genuine change of transmission can still be followed. The
     * full_window flag skips the narrowing outright (the inv_mode
     * escape's far search, which has its own confirmation rule). */
    const int widen_after = p.max_coast > 0 ? 3 * p.max_coast : 30;
    long lo = grid, hi = grid + LINE_SAMPLES - 1;
    if (!full_window && ever_locked && p.fallback_win > 0 &&
        unlocked_for < widen_after) {
        long expected = grid + phi;
        if (expected - p.fallback_win / 2 > lo)
            lo = expected - p.fallback_win / 2;
        if (expected + p.fallback_win / 2 < hi)
            hi = expected + p.fallback_win / 2;
    }

    for (size_t i = 0; i < edges.size(); i++) {
        if (edges[i] < lock_from)
            continue;
        if (edges[i] >= grid + LINE_SAMPLES)
            return -2;            /* chain start beyond this window */
        if (edges[i] < lo || edges[i] > hi)
            continue;             /* outside the allowed neighbourhood
                                     (this also drops edges from an
                                     earlier line: release leaves
                                     lock_from at the last shape edge,
                                     which may be several lines back) */
        if (i < edge_dark.size() && edge_dark[i] >= sync_dark_floor(p))
            continue;             /* too pale to be a sync pulse */
        long cur = edges[i];
        int links = 0;
        bool broken = false, waiting = false;
        for (size_t j = i + 1; links < need; j++) {
            if (j >= edges.size()) {
                if (cur + p.max_period > finalized)
                    waiting = true;
                else
                    broken = true;
                break;
            }
            long per = edges[j] - cur;
            if (per < p.min_period)
                continue;            /* junk edge: skip */
            if (per <= p.max_period) {
                cur = edges[j];
                links++;
                continue;
            }
            if (cur + p.max_period > finalized)
                waiting = true;
            else
                broken = true;
            break;
        }
        if (waiting)
            return -1;
        if (!broken && links >= need)
            return edges[i];
        lock_from = edges[i] + 1;  /* start rejected permanently */
    }
    /* ran out of edges: a chain start could still appear inside the
     * window unless the window is fully behind the horizon */
    if (grid + LINE_SAMPLES > finalized)
        return -1;
    return -2;
}

/* Inverted (WMO phasing) lock acquisition, DEVIATIONS.md #19: streaming
 * twin of syncscan.cpp's find_inv_lock, mirroring try_lock's structure
 * (the two must stay byte-identical - the new fixture's live-vs-batch
 * check in cli/invphasing-test.cpp). Same window rule as try_lock;
 * differences: the chain start's line must be dark-dominant
 * (sync_line_dark - aborts the search, as the batch version) and the run
 * must pass the bright floor. */
long LiveScan::try_inv_lock(long grid)
{
    long finalized = (long)buf.size() - p.max_pulse - 1;
    int need = p.lock_hyst > 0 ? p.lock_hyst : 1;

    const int widen_after = p.max_coast > 0 ? 3 * p.max_coast : 30;
    long lo = grid, hi = grid + LINE_SAMPLES - 1;
    if (ever_locked && p.fallback_win > 0 && unlocked_for < widen_after) {
        long expected = grid + phi;
        if (expected - p.fallback_win / 2 > lo)
            lo = expected - p.fallback_win / 2;
        if (expected + p.fallback_win / 2 < hi)
            hi = expected + p.fallback_win / 2;
    }

    for (size_t i = 0; i < inv_edges.size(); i++) {
        if (inv_edges[i] < inv_lock_from)
            continue;
        if (inv_edges[i] >= grid + LINE_SAMPLES)
            return -2;            /* chain start beyond this window */
        if (inv_edges[i] < lo || inv_edges[i] > hi)
            continue;             /* outside the allowed neighbourhood */
        if (i < inv_bright.size() && inv_bright[i] < sync_bright_floor(p))
            continue;             /* too pale to be a phasing pulse */
        if (!sync_line_dark(sm, grid, p))
            return -2;            /* not phasing: do not look further */
        long cur = inv_edges[i];
        int links = 0;
        bool broken = false, waiting = false;
        for (size_t j = i + 1; links < need; j++) {
            if (j >= inv_edges.size()) {
                if (cur + p.max_period > finalized)
                    waiting = true;
                else
                    broken = true;
                break;
            }
            long per = inv_edges[j] - cur;
            if (per < p.min_period)
                continue;            /* junk edge: skip */
            if (per <= p.max_period) {
                cur = inv_edges[j];
                links++;
                continue;
            }
            if (cur + p.max_period > finalized)
                waiting = true;
            else
                broken = true;
            break;
        }
        if (waiting)
            return -1;
        if (!broken && links >= need)
            return inv_edges[i];
        inv_lock_from = inv_edges[i] + 1;  /* start rejected permanently */
    }
    if (grid + LINE_SAMPLES > finalized)
        return -1;
    return -2;
}

/* min-brightness fallback search over [lo, hi] (same validation as
 * syncscan) */
/* The original's fallback tracker; see syncscan.cpp's fallback_edge, of
 * which this is the streaming twin (the two must stay byte-identical -
 * live-test). Constants are the original's compiled-in ones. */
long LiveScan::fallback_edge(long lo, long hi, long prev) const
{
    const int GATE = 8, GATE_LEVEL = 128;
    long n = (long)sm.size();
    int win = p.fallback_win > 0 ? p.fallback_win : 160;
    if (lo < GATE) lo = GATE;
    if (hi > n - win) hi = n - win;
    if (lo > hi)
        return -1;

    long best = -1;
    int best_mean = 256;
    for (long q = lo; q <= hi; q++) {
        int gate = 0;
        for (int i = 1; i <= GATE; i++)
            gate += sm[q - i] >= p.dark_th ? 255 : 0;
        if (gate / GATE <= GATE_LEVEL)
            continue;
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
            return -1;
    }
    return best;
}

long LiveScan::fallback(long lo, long hi) const
{
    if (lo < 0) lo = 0;
    if (hi >= (long)sm.size()) hi = (long)sm.size() - 1;
    if (lo >= hi)
        return -1;
    long best = lo, sum = 0;
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

void LiveScan::emit(void (*line_cb)(const uint8_t *, int, void *), void *ud,
                    long grid, int how)
{
    if (how == 0)
        lines_locked++;
    else if (how == 1)
        lines_corrected++;
    else
        lines_coasted++;
    if (!line_cb)
        return;
    /* LED/state: tolerate brief misses before showing "coasting"
     * (LReSycn release hysteresis); never-locked lines report 3 */
    int state = how;
    if (how == 2) {
        if (!locked)
            state = 3;
        else if (miss <= p.lock_hyst)
            state = 0;
    }
    uint8_t px[FaxImage::WIDTH];
    for (int i = 0; i < FaxImage::WIDTH; i++)
        px[i] = buf[grid + ((8L * i) / 3 + phi) % LINE_SAMPLES];
    line_cb(px, state, ud);
}

void LiveScan::pump(void (*line_cb)(const uint8_t *, int, void *), void *ud)
{
    while (true) {
        long grid = (long)lines_emitted() * LINE_SAMPLES;
        if (grid + LINE_SAMPLES > (long)buf.size())
            return;              /* window incomplete: wait */

        /* Manual sync align (see nudge_phase). Applied before - and
         * INSTEAD OF - the Sync button transition below: resuming from
         * a hand-placed reference must not clear `locked`, or the next
         * line would re-acquire through a full-window chain search and
         * throw the user's position away. From here it is the ordinary
         * narrow search around the seeded phase, which is what the
         * original does too (its click sets the ever-locked flag
         * alongside the reference offset). */
        if (man_req.load()) {
            man_req.store(false);
            long d = man_delta.load() % LINE_SAMPLES;
            phi = (phi + d + LINE_SAMPLES) % LINE_SAMPLES;
            fb_dir = 0;
            fb_run = 0;
            /* The user has asserted where the sync is. sync_step_lock
             * searches the WHOLE line and would walk straight off to the
             * strongest sync-looking pulse - which is exactly what this
             * feature exists to override (cli/manual-sync-test.cpp). Hold
             * it off until the decoder earns a shape lock of its own. */
            manual_hold = true;
            track_applied = true;
            locked = true;
            ever_locked = true;
            miss = 0;
            since_shape = 0;
            last_shape = grid + phi;
        }

        /* Sync button transition (docs/01 sec. 3.2 "Sync-track
         * enable"). Re-enable: reset the state machine and re-acquire
         * from the next window. */
        bool tr = track.load();
        if (tr != track_applied) {
            track_applied = tr;
            if (tr) {
                locked = false;
                inv_mode = false;
                miss = 0;
                since_shape = 0;
                while (!edges.empty() && edges.front() < grid) {
                    edges.pop_front();
                    edge_dark.pop_front();
                }
                lock_from = grid;
                while (!inv_edges.empty() && inv_edges.front() < grid) {
                    inv_edges.pop_front();
                    inv_bright.pop_front();
                }
                inv_lock_from = grid;
                esc_prev = -1;
                esc_run = 0;
            }
        }

        int how = 3;   /* 0 shape / 1 fallback / 2 coast / 3 unlocked */

        if (track_applied) {
            long expected = grid + phi;
            /* once per line, matching syncscan's counter exactly */
            if (grid != counted_grid) {
                counted_grid = grid;
                if (!locked)
                    unlocked_for++;
                else
                    unlocked_for = 0;
                /* consecutive black-dominant lines, counted once per line
                 * even though pump() may re-enter on this one waiting for
                 * samples. The batch twin counts it inside the inv_mode
                 * branch, which runs on exactly these lines. */
                if (inv_mode) {
                    if (sync_line_dark(sm, grid, p))
                        inv_dark_run++;
                    else
                        inv_dark_run = 0;
                }
            }

            if (!locked) {
                long E = try_lock(grid);
                if (E == -1)
                    return;      /* undecidable yet: wait */
                if (E >= 0) {
                    phi = (sync_anchor(sm, E, p) - grid + LINE_SAMPLES)
                          % LINE_SAMPLES;
                    fb_dir = 0;
                    fb_run = 0;
                    manual_hold = false;
                    last_shape = E;
                    if (ever_locked)
                        relocks++;
                    locked = true;
                    unlocked_for = 0;
                    ever_locked = true;
                    miss = 0;
                    since_shape = 0;
                    how = 0;
                }
                /* E == -2: no chain in this window -> fallback below */
                if (how != 0 && !ever_locked) {
                    /* WMO inverted phasing (DEVIATIONS.md #19): a chain
                     * of white pulses on dark lines. The anchor is the
                     * pulse's trailing edge plus SYNC_INV_OFFSET (i.e.
                     * its leading edge).
                     * Only before the first lock of the stream: once ANY
                     * phase reference exists, the established convention
                     * owns it (see the !locked branch in syncscan.cpp). */
                    long IE = try_inv_lock(grid);
                    if (IE == -1) {
                        if (finishing)
                            IE = -2;   /* end of stream: an incomplete
                                        * chain is no chain, as batch */
                        else
                            return;    /* undecidable yet: wait */
                    }
                    if (IE >= 0) {
                        phi = (IE + SYNC_INV_OFFSET - grid + LINE_SAMPLES)
                              % LINE_SAMPLES;
                        /* start measuring the line period here (batch
                         * twin in syncscan.cpp) */
                        inv_drift.reset();
                        inv_drift.add(grid / LINE_SAMPLES,
                                      IE + SYNC_INV_OFFSET);
                        inv_period = 4000.0;
                        inv_phase = (double)phi;
                        inv_dark_run = SYNC_PHASING_CONFIRM;
                        fb_dir = 0;
                        fb_run = 0;
                        manual_hold = false;
                        last_shape = IE;
                        if (ever_locked)
                            relocks++;
                        locked = true;
                        inv_mode = true;
                        unlocked_for = 0;
                        ever_locked = true;
                        miss = 0;
                        since_shape = 0;
                        how = 0;
                    }
                    /* IE == -2: no inverted chain either -> fallback */
                }
            } else if (inv_mode) {
                /* Holding a phase acquired from WMO phasing
                 * (DEVIATIONS.md #19): no fallback, no release - the
                 * picture carries no pulse to track. */
                long win = p.fallback_win > 0 ? p.fallback_win : 160;
                long bracket = p.search_win + win / 2;
                /* escape: a black-pulse chain means a JMH-style station
                 * took over the frequency. Narrow first (its own phasing
                 * will already have re-anchored us below); then the whole
                 * line, but a far result must CONFIRM on the next line -
                 * a real station's pulses chain at the same phase on
                 * consecutive lines, picture content does not. Skipped
                 * entirely on black-dominant lines (VMW's end-of-chart
                 * band chains like a pulse). See the same branch in
                 * syncscan.cpp (byte-identical). */
                long E = -2;
                bool dark = sync_line_dark(sm, grid, p);
                if (!dark) {
                E = try_lock(grid);
                if (E == -1) {
                    if (finishing)
                        E = -2;   /* end of stream: an incomplete
                                   * chain is no chain, as batch */
                    else
                        return;    /* undecidable yet: wait */
                }
                if (E == -2) {
                    E = try_lock(grid, true);
                    if (E == -1) {
                        if (finishing)
                            E = -2;
                        else
                            return;
                    }
                    if (E >= 0) {
                        long f = (E - grid + LINE_SAMPLES) % LINE_SAMPLES;
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
                            E = -2;         /* not proven yet */
                    }
                    /* no chain on this line: esc_prev/esc_run are kept -
                     * see the same branch in syncscan.cpp */
                }
                }
                if (E >= 0) {
                    phi = (sync_anchor(sm, E, p) - grid + LINE_SAMPLES)
                          % LINE_SAMPLES;
                    fb_dir = 0;
                    fb_run = 0;
                    manual_hold = false;
                    last_shape = E;
                    relocks++;
                    locked = true;
                    inv_mode = false;
                    unlocked_for = 0;
                    miss = 0;
                    since_shape = 0;
                    how = 0;
                } else if (dark && inv_dark_run >= SYNC_PHASING_CONFIRM) {
                    /* still (or again) phasing: re-anchor on this line's
                     * pulse, so the next chart's preamble re-seats the
                     * phase and keeps the period measurement going. The
                     * run-length gate keeps a chart's own dark bands from
                     * re-anchoring mid-picture (batch twin). */
                    if (inv_dark_run == SYNC_PHASING_CONFIRM &&
                        inv_drift.have && grid / LINE_SAMPLES - inv_drift.first
                                          > SYNC_DRIFT_MIN_PTS)
                        inv_drift.reset();   /* a NEW preamble */
                    long prev_edge = grid - LINE_SAMPLES + phi;
                    /* bracket the RAW edges, SYNC_INV_OFFSET away from the
                     * anchor the prediction is in (batch twin) */
                    long ecen = expected - SYNC_INV_OFFSET;
                    while (!inv_edges.empty() &&
                           inv_edges.front() < ecen - bracket) {
                        inv_edges.pop_front();
                        inv_bright.pop_front();
                    }
                    /* same finalization rule as the shape match below */
                    if (!finishing &&
                        (long)buf.size() <= ecen + bracket + p.max_pulse)
                        return;    /* undecidable yet: wait */
                    for (size_t k = 0;
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
                            /* every preamble anchor feeds the period fit
                             * (batch twin) */
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
                    /* The content step below needs one line of lookahead,
                     * so wait for it BEFORE touching inv_phase - pump()
                     * re-enters on this line, and a half-applied hold
                     * would advance the phase twice. */
                    bool have_next =
                        (long)buf.size() >= grid + 2 * LINE_SAMPLES;
                    if (!dark && !finishing && !have_next)
                        return;
                    /* hold the phase at the MEASURED line period, not at
                     * the nominal 4000 (DEVIATIONS.md #19, batch twin) */
                    inv_phase += inv_period - (double)LINE_SAMPLES;
                    if (inv_phase < 0.0)
                        inv_phase += (double)LINE_SAMPLES;
                    else if (inv_phase >= (double)LINE_SAMPLES)
                        inv_phase -= (double)LINE_SAMPLES;
                    phi = (long)(inv_phase + 0.5) % LINE_SAMPLES;
                    /* ... and follow a dropout off the picture's own
                     * content (batch twin) */
                    if (!dark) {
                        long lag = sync_content_step(sm, grid, phi,
                                                     have_next);
                        if (lag != 0) {
                            inv_phase += (double)lag;
                            while (inv_phase < 0.0)
                                inv_phase += (double)LINE_SAMPLES;
                            while (inv_phase >= (double)LINE_SAMPLES)
                                inv_phase -= (double)LINE_SAMPLES;
                            phi = (long)(inv_phase + 0.5) % LINE_SAMPLES;
                            relocks++;
                        }
                    }
                    miss++;
                    since_shape++;
                    how = 2;
                }
            } else {
                /* shape match: candidate edge near the predicted edge;
                 * drop edges that fell behind the match window. The
                 * period is checked against the PREVIOUS line's edge
                 * position (grid+phi), which stays ~4000 even across
                 * fallback/coast lines. */
                long prev_edge = grid - LINE_SAMPLES + phi;
                long win = p.fallback_win > 0 ? p.fallback_win : 160;
                long bracket = p.search_win + win / 2;
                while (!edges.empty() && edges.front() < expected - bracket) {
                    edges.pop_front();
                    edge_dark.pop_front();
                }
                /* Nothing here is final until any run starting inside the
                 * bracket has ended (max_pulse) - which also covers the
                 * 3*win/2 that sync_anchor reads past a candidate, so the
                 * anchor comes out the same as the batch scanner's
                 * (live-test). */
                if (!finishing &&
                    (long)buf.size() <= expected + bracket + p.max_pulse)
                    return;       /* undecidable yet: wait */
                for (size_t k = 0;
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
                        manual_hold = false;
                        last_shape = edges[k];   /* raw: indexes `edges` */
                        miss = 0;
                        since_shape = 0;
                        how = 0;
                    }
                    break;
                }
            }

            if (how != 0 && !inv_mode) {
                /* fallback: full window until first lock, narrow
                 * window after (docs/01 sec. 3.2(8)). (Skipped in
                 * inv_mode: a WMO-phasing picture has no pulse to
                 * find - holding the phase is correct there.) */
                long fb = -1;
                bool snapped = false;
                long flo = grid, fhi = grid + LINE_SAMPLES - 1;
                bool have = true;
                if (ever_locked) {
                    if (p.fallback_win > 0) {
                        flo = expected - p.fallback_win / 2;
                        fhi = expected + p.fallback_win / 2;
                    } else {
                        have = false;
                    }
                    /* Whole-line step first - mirrors syncscan exactly.
                     * It needs the NEXT line too (one line of lookahead,
                     * syncscan.h), so hold this line back until that data
                     * has arrived. finish() drops the wait so the last
                     * line of a stream still comes out, exactly as the
                     * batch scanner skips the check when the file ends. */
                    if (!manual_hold) {
                        if (!finishing &&
                            (long)buf.size() < grid + 2 * LINE_SAMPLES)
                            return;
                        fb = sync_step_lock(sm, grid, phi, p);
                        snapped = fb >= 0;
                    }
                }
                if (fb < 0 && have) {
                    /* ported tracker first, our dip-depth search as the
                     * second chance - mirrors syncscan exactly */
                    fb = fallback_edge(flo, fhi, expected);
                    bool ported = fb >= 0;
                    if (fb < 0)
                        fb = fallback(flo, fhi);
                    /* hold rather than slew onto dark picture, and only on
                     * our second chance - see the same test in
                     * syncscan.cpp for why */
                    if (!ported && fb >= 0 &&
                        sync_run_mean(sm, fb, p) >= sync_dark_floor(p))
                        fb = -1;
                }
                if (fb >= 0) {
                    /* one reference for every path - see syncscan.cpp.
                     * sync_anchor reads 3*win/2 past its argument, so hold
                     * the line back until that has arrived, or the live
                     * anchor would be computed on less video than the batch
                     * one (live-test). */
                    long aw = p.fallback_win > 0 ? p.fallback_win : 160;
                    if (!finishing && (long)buf.size() < fb + 3 * aw / 2 + 1)
                        return;
                    fb = sync_anchor(sm, fb, p);
                    long tgt = (fb - grid + LINE_SAMPLES) % LINE_SAMPLES;
                    if (snapped) {
                        phi = tgt;
                        since_shape = 0;
                    } else {
                        phi = sync_slew(phi, tgt, p, fb_dir, fb_run);
                    }
                    how = 1;
                    miss = 0;
                } else {
                    miss++;
                    how = 2;
                }
                since_shape++;
                /* full release (RReSycn): re-acquire on next lines */
                if (locked && since_shape > p.max_coast) {
                    locked = false;
                    while (!edges.empty() && edges.front() <= last_shape) {
                        edges.pop_front();
                        edge_dark.pop_front();
                    }
                    lock_from = last_shape + 1;
                }
            }
        }

        emit(line_cb, ud, grid, how);
    }
}
