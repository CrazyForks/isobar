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
 *   past expected+search_win+max_pulse have arrived
 * - lock acquisition waits until the whole hysteresis chain is decidable
 * Buffers keep the whole stream (~0.5 MB/min of video) - fine for a
 * 19-minute reception; reset() between receptions to free it.
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

    /* stats (same meanings as FaxImage) */
    int lines_locked;
    int lines_corrected;
    int lines_coasted;
    int relocks;

    int lines_emitted() const;

private:
    SyncParams p;

    std::vector<uint8_t> buf;   /* whole video stream so far       */
    std::vector<int> sm;        /* 8-sample moving average of buf  */
    long acc;                   /* running MA sum                  */
    std::deque<long> edges;     /* detected sync-edge candidates   */
    long run_start;             /* dark-run start, -1 = not in run */

    /* line grid + phase (docs/01 sec. 3.2): line n covers samples
     * [n*4000, (n+1)*4000), emitted rotated by phi; tracking only
     * adjusts phi, the grid never moves */
    long phi;            /* rotation offset within the line window */
    bool ever_locked;    /* fallback search: full window until then */
    bool locked;         /* currently locked (release clears)       */
    long last_shape;     /* absolute pos of last shape-locked edge  */
    int miss, since_shape;
    long lock_from;      /* smallest edge position not rejected for lock */

    std::atomic<bool> track;    /* Sync button, written by UI thread  */
    bool track_applied;         /* last state acted on (audio thread) */
    std::atomic<long> man_delta;   /* pending manual align, samples   */
    std::atomic<bool> man_req;     /* manual align posted by UI thread */

    void pump(void (*line_cb)(const uint8_t *, int, void *), void *ud);
    long try_lock(long grid);   /* >=0 edge / -1 wait / -2 no chain  */
    long fallback(long lo, long hi) const;
    void emit(void (*line_cb)(const uint8_t *, int, void *), void *ud,
              long grid, int how);
};

#endif
