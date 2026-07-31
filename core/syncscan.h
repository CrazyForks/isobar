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

    /* stats */
    int lines_locked;     /* lines emitted on a detected sync edge */
    int lines_corrected;  /* lines emitted on the fallback search */
    int lines_coasted;    /* lines emitted on predicted position */
    int relocks;          /* times lock was re-acquired after losing it */
};

/* Tunable sync-scan thresholds. Defaults are exactly the values that
 * reproduce the historical M0 behavior bit for bit on clean signals.
 * The GUI's "Details" dialog reaches these via the settings file
 * (docs/01-program-analysis.md sec. 5-6):
 *   Sync2Thre -> dark_th       SyncThre -> fb_thresh
 *   SyncWidth (10*n ms) -> min/max pulse
 *   LReSycn -> lock_hyst       RReSycn -> max_coast
 *   syn combo -> fallback_win = 40*(i+1) samples (= 5*(i+1) ms @ 8 kHz) */
struct SyncParams {
    int min_period;   /* 3980 samples: shortest accepted sync period */
    int max_period;   /* 4020 */
    int min_pulse;    /*  100 samples: shortest sync pulse (12.5 ms) */
    int max_pulse;    /*  400 samples: longest sync pulse (50 ms)    */
    int search_win;   /*   40 samples: +- window for shape-edge match */
    int max_coast;    /*   60 lines: give up lock after this many misses */
    int dark_th;      /*   96: brightness below this = "dark"        */
    int lock_hyst;    /*    5: valid periods to lock; misses before
                        "coasting" state (LReSycn hysteresis)         */
    int fallback_win; /*  160 samples: min-brightness search width
                        (syn combo default 20 ms); 0 disables fallback */
    int fb_thresh;    /*   10: min dip depth below window mean for a
                        valid fallback edge (SyncThre)                */
};

SyncParams sync_default_params();

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
