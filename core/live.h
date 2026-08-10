/* live.h - streaming WEFAX line assembly (live audio counterpart of
 * syncscan.h).
 *
 * Same algorithms as scan_lines (docs/01 sec. 3.2(7)(8)(10)) but fed
 * incrementally: video bytes arrive in chunks from the sound card and
 * each completed 1500-px line is pushed out through a callback. Lines
 * are emitted on a fixed 4000-sample grid from stream start, rotated
 * by the tracked phase offset - including the pre-lock preamble, like
 * the original. On the same input stream it produces the same lines
 * as scan_lines (verified by cli/live-test.cpp).
 *
 * Streaming notes:
 * - a dark run is only known to be a sync pulse candidate once a
 *   non-dark sample ends it, so edge decisions lag by up to max_pulse
 * - "no edge near the predicted position" is only decidable once bytes
 *   past expected+search_win+fallback_win/2+max_pulse have arrived
 *   (plus up to 3*win/2 more when the anchor refinement runs)
 * - lock acquisition waits until the whole hysteresis chain is decidable
 * - a completed line is held back until the NEXT line has arrived, because
 *   sync_step_lock() confirms a step with one line of lookahead; finish()
 *   releases the held line at end of stream
 * Buffers keep the whole stream (~2.4 MB/min: video bytes plus the
 * same-length smoothed int vector) - fine for a 19-minute reception;
 * the GUI frees it by deleting and re-creating the LiveState between
 * receptions.
 */
#ifndef ISOBAR_LIVE_H
#define ISOBAR_LIVE_H

#include "syncscan.h"

#include <atomic>
#include <vector>
#include <deque>
#include <cstddef>
#include <cstdint>

struct LiveScan {
    explicit LiveScan(const SyncParams &p = sync_default_params());
    void reset();

    /* Sync button (docs/01 sec. 3.2 "Sync-track enable"): with tracking
     * OFF lines still come out at a fixed 4000-sample advance with the
     * phase frozen, but no lock/correction/coasting runs and lines are
     * reported with state 3. Thread-safe: may be called from the UI
     * thread while feed() runs on the audio thread. Default ON. */
    void set_track(bool on);

    /* Manual sync align (docs/01 sec. 3.2 "Sync-track enable" and sec. 4
     * "Zoom/pan"; the original readme's 手動同期位置指定): with tracking
     * OFF the user points at where the sync signal really is, and the
     * sync reference moves `delta` samples along the line (wrapped into
     * one line, may be negative) and tracking resumes FROM THERE - the
     * next line's edge search centres on the position the user gave
     * instead of re-acquiring a fresh lock, which is the whole point
     * when the signal is too noisy or too picture-like to lock on its
     * own. Turns tracking back on by itself, like the original's click
     * (which presses the Sync button programmatically).
     * Thread-safe: may be called from the UI thread while feed() runs
     * on the audio thread; applied at the next line boundary. */
    void nudge_phase(long delta);

    /* Feed n video bytes (8000 S/s). For each completed line calls
     *     line_cb(line1500, state, ud)
     * with state 0 locked / 1 corrected / 2 coasting / 3 tracking off
     * (as scan_lines). line_cb may be null (stats still advance). */
    void feed(const uint8_t *data, size_t n,
              void (*line_cb)(const uint8_t *line1500, int state, void *ud),
              void *ud);

    /* End of stream. sync_step_lock() needs one line of lookahead, so a
     * completed line is held back until the line after it has arrived;
     * this emits the held line without that confirmation, which is what
     * the batch scanner does when a file ends. Without it the last line
     * of a reception never comes out. NOT thread-safe: call it on the
     * thread that calls feed(). */
    void finish(void (*line_cb)(const uint8_t *line1500, int state,
                                void *ud), void *ud);

    /* Thread-safe form of finish(), for a UI thread that cannot call it
     * directly: request_finish() posts the request, the next feed() on
     * the audio thread performs the flush with that feed's callback, and
     * finish_done() reports when it has. The caller should wait briefly
     * for finish_done() before it stops accepting lines, or the flushed
     * line is emitted into a closed gate and lost anyway. */
    void request_finish();
    bool finish_done() const { return fin_done.load(); }

    /* stats (same meanings as FaxImage). Atomic: pump() writes them on
     * the audio thread while the UI thread reads them on Scan-off
     * (record_off) and in --test-scan. */
    std::atomic<int> lines_locked;
    std::atomic<int> lines_corrected;
    std::atomic<int> lines_coasted;
    std::atomic<int> relocks;

    int lines_emitted() const;

private:
    SyncParams p;

    std::vector<uint8_t> buf;   /* whole video stream so far       */
    std::vector<int> sm;        /* 8-sample moving average of buf  */
    long acc;                   /* running MA sum                  */
    std::deque<long> edges;     /* detected sync-edge candidates   */
    std::deque<int> edge_dark;  /* each one's run-mean brightness;
                                   kept in lockstep with `edges`   */
    SyncRuns runs;              /* dark-run detector (syncscan.h)  */
    /* WMO inverted phasing (DEVIATIONS.md #19): white-pulse candidates,
       positions at each run's END (SYNC_INV_OFFSET carries it to the
       line start, which is the pulse's leading edge) */
    std::deque<long> inv_edges;
    std::deque<int> inv_bright; /* kept in lockstep with inv_edges */
    SyncRuns inv_runs;          /* bright-run detector (syncscan.h) */

    /* line grid + phase (docs/01 sec. 3.2): line n covers samples
     * [n*4000, (n+1)*4000), emitted rotated by phi; tracking only
     * adjusts phi, the grid never moves */
    long phi;            /* rotation offset within the line window */
    int fb_dir, fb_run;  /* far-fallback run, for sync_slew (syncscan.h) */
    bool finishing;      /* end of stream: emit without lookahead        */
    bool manual_hold;    /* hand-placed phase outranks sync_step_lock    */
    bool ever_locked;    /* fallback search: full window until then */
    bool locked;         /* currently locked (release clears)       */
    long last_shape;     /* absolute pos of last shape-locked edge  */
    int miss, since_shape;
    int unlocked_for;    /* consecutive lines with no lock (widen search) */
    long counted_grid;   /* line unlocked_for was last updated for: pump()
                            re-enters on the same line while it waits for
                            samples, and the count is per line */
    long lock_from;      /* smallest edge position not rejected for lock */
    bool inv_mode;       /* holding a WMO-phasing phase (DEVIATIONS #19) */
    long inv_lock_from;  /* smallest inv-edge position not rejected      */
    long esc_prev;       /* last full-window escape chain's phase        */
    int esc_run;         /* consecutive lines agreeing on it             */
    int inv_dark_run;    /* consecutive black-dominant lines             */
    SyncInvDrift inv_drift;   /* line period fitted to the preamble      */
    double inv_period;   /* samples per line while coasting (DEV #19)    */
    double inv_phase;    /* exact coasting phase (fractional)            */

    std::atomic<bool> track;    /* Sync button, written by UI thread  */
    bool track_applied;         /* last state acted on (audio thread) */
    std::atomic<long> man_delta;   /* pending manual align, samples   */
    std::atomic<bool> man_req;     /* manual align posted by UI thread */
    std::atomic<bool> fin_req;     /* flush posted by the UI thread    */
    std::atomic<bool> fin_done;    /* ... and performed by feed()      */

    void pump(void (*line_cb)(const uint8_t *, int, void *), void *ud);
    /* >=0 edge / -1 wait / -2 no chain; full_window ignores the
     * ever_locked neighbourhood rule (the inv_mode escape's far search) */
    long try_lock(long grid, bool full_window = false);
    long try_inv_lock(long grid);   /* same, for WMO phasing pulses  */
    void emit(void (*line_cb)(const uint8_t *, int, void *), void *ud,
              long grid, int how);
};

#endif
