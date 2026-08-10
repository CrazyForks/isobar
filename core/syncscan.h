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
#include <cmath>     /* sqrt, for the content-step correlation below */
#include <cstdint>
#include <cstddef>   /* size_t — older libstdc++ doesn't leak it via <vector>,
                      * so gcc-11 (ubuntu-22.04-arm) rejects the unqualified
                      * uses in sync_anchor below. Same fix the .cpp files
                      * needed in v1.1.1; the header grew its own in v1.5.0. */

struct FaxImage {
    static const int WIDTH = 1500;     /* pixels per line                   */
    static const int MAX_LINES = 2280; /* 19 min @ 120 rpm (docs/01 sec. 4) */
    /* Receive-buffer-only cap (DEVIATIONS #17): the GUI lets the image
     * grow past MAX_LINES (e.g. XSG charts ~2755 lines) up to this limit,
     * dropping the oldest line beyond it. .syn I/O stays capped at
     * MAX_LINES for KG-FAX compatibility. */
    static const int HARD_MAX_LINES = 4560;
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
 * (docs/01-program-analysis.md sec. 6) - except dark_th, which stays at
 * ours because our detector computes that quantity differently from the
 * original's, and fb_thresh, which is our own second-chance search with
 * no ini key at all (DEVIATIONS.md #16).
 * The GUI's "Details" dialog reaches these via the settings file
 * (docs/01 sec. 5-6):
 *   Sync2Thre -> dark_th       SyncThre  -> fb_mean
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
    int search_win;   /*   20 samples: +- window the shape check judges
                        the ANCHOR within; raw edges are bracketed more
                        generously (an anchor sits within win/2 of its
                        own edge), then the anchor itself is tested
                        (SyncWidth)                                   */
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
                        declines; hard-coded, no ini key (DEVIATIONS
                        #16)                                            */
    int fb_mean;      /*   30: the original's SyncThre, in its own
                        units at last - max boxcar mean (0..255 over
                        the binarised video) for a valid edge on the
                        ported fallback (docs/01 sec. 3.2(8)); this is
                        what the ini's FallbackDepth sets              */
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

/* ---- Front end, shared by scan_lines and LiveScan ----
 *
 * The batch scanner walks a finished buffer and the live one is fed a
 * sample at a time, but the arithmetic below has to be IDENTICAL between
 * them or the two paths decode the same audio differently. It was written
 * out twice; these are the single copies. */

/* One sample of the 8-sample moving average every detector here runs on.
 * `acc` is the caller's running sum and `old` the sample 8 back (unused
 * while i < 8). The ramp-up divisor - fewer than 8 samples averaged over
 * the first 8 - is the fiddly part, and the reason this is a function. */
inline int sync_ma_step(long &acc, size_t i, uint8_t cur, uint8_t old)
{
    acc += cur;
    if (i >= 8)
        acc -= old;
    return (int)(acc / (i >= 7 ? 8 : (long)i + 1));
}

/* Incremental collector for the runs both detectors look for: a stretch of
 * samples on one side of a threshold whose length is a plausible sync-pulse
 * width (docs/01 sec. 3.2(7)). Polarity and reference point are the
 * caller's:
 *   sync pulse  - DARK run, position published at its START
 *   WMO phasing - BRIGHT run, position published at its END
 *                 (DEVIATIONS.md #19; SYNC_INV_OFFSET then carries it to
 *                 the line's first pixel)
 * Feed every sample in order; step() answers true on the sample that ENDS
 * a qualifying run. */
struct SyncRuns {
    long start;      /* first sample of the current run, -1 = not in one */

    void reset() { start = -1; }

    /* `hit` = this sample is on the run's side of the threshold. On a true
     * return *pos is the published position and *mean the run's mean
     * brightness - what tells a sync pulse from picture content. */
    bool step(const std::vector<int> &sm, size_t i, bool hit,
              const SyncParams &p, bool publish_end, long *pos, int *mean)
    {
        if (hit) {
            if (start < 0)
                start = (long)i;
            return false;
        }
        if (start < 0)
            return false;
        long from = start;
        start = -1;
        long len = (long)i - from;
        if (len < p.min_pulse || len > p.max_pulse)
            return false;
        long s = 0;
        for (long k = 0; k < len; k++)
            s += sm[(size_t)(from + k)];
        *pos = publish_end ? (long)i : from;
        *mean = (int)(s / len);
        return true;
    }
};

/* ---- WMO inverted phasing (DEVIATIONS.md #19) ----
 *
 * The machinery above knows one sync shape: the JMH-style black pulse in a
 * white signal. WMO-standard phasing is the inverse: a BLACK signal with
 * one WHITE pulse per line (IOC 576: 25 ms = 5% of the line), sent for at
 * least 30 s before each chart. VMW (Wiluna) works exactly this way and
 * sends NO per-line pulse during the picture at all, so the black-pulse
 * detector never locks (0.9% on a 646 s off-air recording) and the
 * fallback then wanders the phase across picture content (133 false
 * corrections) - the decoded chart looks mirrored/shredded.
 *
 * The inverted path below acquires the phase from the white pulse and
 * then HOLDS it (inv_mode in scan_lines/LiveScan): no fallback, no
 * release, because the picture carries nothing to track - coasting is
 * exactly right there. The next chart's phasing re-anchors the phase
 * through the inverted shape match, which also tracks clock drift.
 *
 * Brightness floor: the mirror of sync_dark_floor. A white phasing pulse
 * is genuinely white (~230-251 against black ~20-60 on the VMW
 * recording), so halfway between dark_th and full scale separates it
 * from mid-grey picture content. Ours, no ini key. */
inline int sync_bright_floor(const SyncParams &p) { return (p.dark_th + 255) / 2; }

/* Offset from the white phasing pulse's trailing edge to the line's first
 * pixel: -196 samples, i.e. the pulse's LEADING edge (its width is 196
 * samples here and 196-197 on the two JMH fixtures - the WMO 25 ms). The
 * plain reading of the standard: the line begins with 5% white.
 *
 * Where the seam sits matters more than it looks, because the line wraps
 * there. On a station with per-line sync the port has no choice - the
 * phase IS the sync pulse - but a WMO-phasing picture carries no sync, so
 * the anchor is a convention, and the one thing it must do is fall off the
 * page. VMW's chart occupies about two thirds of the drum, leaving a
 * 500 px blank margin, and the anchor has to land in it:
 *
 *   anchor                       page moves   seam lands
 *   JMH's own convention (+58)       0 px     21 px INSIDE the page - the
 *                                             masthead's first letter and
 *                                             the panel border wrapped to
 *                                             the far right
 *   pulse trailing edge (0)         22 px     2 px into the margin
 *   pulse leading edge (-196)       95 px     75 px into the margin
 *
 * (+58 is what JMH itself uses: on `jmh sample.wav` and
 * `jmh-kiwi-testchart.wav` the phase its picture sync holds sits +61 and
 * +60 past the phasing pulse's trailing edge, measured over 118 and 55
 * preamble lines. Worth knowing, and it is what the port anchors JMH at -
 * but that convention is about where JMH puts its sync strip, not about
 * where a chart's page begins, and following it costs VMW a sliver of its
 * own chart.) Our constant, no ini key; applied at every inverted anchor
 * (acquisition and shape match, batch and live). */
inline const int SYNC_INV_OFFSET = -196;

/* Confirmation length for the far escape out of inv_mode (DEVIATIONS.md
 * #19): the chain must appear at the same phase on this many lines before
 * the decoder believes a JMH-style station took over. Two agreeing lines
 * (the sync_step_lock philosophy) are NOT enough here: a tall chart
 * feature is a per-line pulse too - VMW's end-of-chart band contains a
 * 70 px black bar that chained pitch-black 180-199-sample runs at period
 * ~3998 for 12 consecutive lines (measured at lines 1112-1123 of the VMW
 * recording). A real station's strip repeats for the whole picture, so
 * 20 keeps the takeover working while chart decoration cannot reach it.
 * Lines with no chain at all do not reset the count (a pulse embedded in
 * photo content is missed on some lines). */
inline const int SYNC_ESC_CONFIRM = 20;

/* Consecutive black-dominant lines carrying a pulse at the tracked phase
 * before a re-anchor mid-stream is believed to be a phasing preamble
 * (DEVIATIONS.md #19). A real preamble runs at least 30 s = 60 lines; a
 * chart's own dark bands are far shorter - measured on the VMW recording,
 * lines 351-352 (2) and 383-386 (4) mean out below dark_th inside the
 * picture, and each one re-anchored the phase up to search_win, walking
 * the image sideways in visible steps. Eight lines clears every band on
 * that recording (the end-of-chart band is 21, but it is solid black and
 * offers no pulse to re-anchor on) and costs nothing on a real preamble,
 * which is held on the acquisition chain's own count anyway. */
inline const int SYNC_PHASING_CONFIRM = 8;

/* Line period estimated from the phasing preamble (DEVIATIONS.md #19).
 *
 * A station without per-line sync gives the decoder exactly one chance to
 * measure the line rate: the phasing pulses. The nominal 4000 samples is
 * NOT the rate that arrives - a KiwiSDR's 12 kHz stream is really
 * ~12000.96 Hz, which is 3999.68 samples per line (measured by fitting 57
 * preamble pulses of the VMW recording, residual max 1.7 samples). Held at
 * exactly 4000, the phase walks 0.32 samples per line: 412 samples = 154 px
 * of shear across one chart, which is what made the decoded chart look
 * sheared with content cut at the seam. A station WITH per-line sync never
 * shows this - its tracking absorbs the drift line by line.
 *
 * So: least squares over the preamble anchors (line index k against anchor
 * position, both exact), and the coasting phase then advances by the fitted
 * period instead of 4000. Least squares rather than first-to-last because
 * the anchors jitter ~1.7 samples: the fit's slope error over 57 lines is
 * 0.014 samples/line (5 px over a chart) against 0.04 (17 px) for the
 * endpoints alone.
 *
 * `add` takes absolute anchor positions, so a wrap of the in-line phase
 * cannot corrupt the fit; callers reset() at each acquisition and at the
 * start of each new preamble, keeping the previous estimate meanwhile.
 * Shared by scan_lines and LiveScan so the two stay byte-identical. */
inline const int SYNC_DRIFT_MIN_PTS = 20;   /* anchors before a fit is used */
inline const double SYNC_DRIFT_MAX = 8.0;   /* samples/line, 0.2%: a bigger
                                             * slope is a bad fit, not a
                                             * clock                        */

struct SyncInvDrift {
    double n, sk, skk, sr, skr;   /* least-squares sums over (k, r) */
    long first;                   /* line index of the first anchor */
    long last_line, last_anchor;  /* previous point, for unwrapping   */
    bool have;

    void reset()
    {
        n = sk = skk = sr = skr = 0.0;
        first = 0;
        last_line = 0;
        last_anchor = 0;
        have = false;
    }

    /* `anchor` is an absolute position; whether it landed just before or
     * just after its line's window boundary is an accident of the phase,
     * and a point a whole line away from its neighbours would swamp the
     * fit (measured: the slope ran off to the clamp and the estimate fell
     * back to the nominal 4000). So each point is unwrapped onto the
     * previous one first. */
    void add(long line, long anchor)
    {
        if (!have) {
            first = line;
            have = true;
        } else {
            long want = last_anchor + 4000 * (line - last_line);
            while (anchor - want > 2000)
                anchor -= 4000;
            while (anchor - want < -2000)
                anchor += 4000;
        }
        last_line = line;
        last_anchor = anchor;
        double k = (double)(line - first);
        double r = (double)(anchor - 4000 * line);   /* residual vs nominal */
        n += 1.0;
        sk += k;
        skk += k * k;
        sr += r;
        skr += k * r;
    }

    /* samples per line, or 4000 while the fit is not yet trustworthy */
    double period() const
    {
        double den = n * skk - sk * sk;
        if (n < (double)SYNC_DRIFT_MIN_PTS || den <= 0.0)
            return 4000.0;
        double b = (n * skr - sk * sr) / den;
        if (b > SYNC_DRIFT_MAX || b < -SYNC_DRIFT_MAX)
            return 4000.0;
        return 4000.0 + b;
    }
};

/* ---- Content re-alignment for pulse-free pictures (DEVIATIONS.md #19) ----
 *
 * A held phase is only as good as the stream feeding it. A networked SDR
 * drops audio: the VMW recording loses 60-80 ms three times inside one
 * chart, and every lost sample moves the rest of the picture sideways for
 * good. A station with per-line sync shrugs this off - sync_step_lock
 * re-locks on the line the step happens. A WMO-phasing station sends
 * nothing to re-lock to, so the picture ITSELF is the only reference left.
 *
 * It is a good one. A fax line's content does not move: the drum turns at
 * a constant rate, so any lag between one line and the next that is not
 * the measured drift is a timing fault, and the same lag then holds for
 * every following line. Measured on the VMW recording, the three real
 * steps stand out completely - at the step the correlation peaks at
 * 0.37-0.84 against 0.08-0.18 at zero lag, while ordinary picture lines
 * peak at zero. Corrected, the chart's panel border runs straight down
 * 600 lines at one column (it walked 972 -> 744 -> 507 -> 316 before), and
 * the page lands inside the line with a white margin at both ends.
 *
 * The search is decimated by SYNC_STEP_DEC for the coarse pass and refined
 * at full rate, and a candidate must survive four tests: it moves further
 * than the drift model ever would (SYNC_STEP_MIN), the peak is a real
 * match (SYNC_STEP_CORR) that beats no-move by SYNC_STEP_MARGIN, and the
 * NEXT line agrees with it to SYNC_STEP_TOL - the one-line lookahead
 * sync_step_lock uses, for the same reason: a real step persists, a chance
 * resemblance between two rows does not. inv_mode only. */
inline const long SYNC_STEP_MAX_LAG = 800;   /* +-300 px of search      */
inline const int SYNC_STEP_DEC = 4;          /* coarse-pass decimation  */
inline const long SYNC_STEP_MIN = 20;        /* smallest lag acted on   */
inline const long SYNC_STEP_TOL = 12;        /* next-line agreement     */
inline const double SYNC_STEP_CORR = 0.35;   /* peak must be a match    */
inline const double SYNC_STEP_CORR2 = 0.30;  /* ... on the next line    */
inline const double SYNC_STEP_MARGIN = 0.10; /* ... and beat no-move by */
inline const double SYNC_STEP_RIVAL = 0.10;  /* ... and every other lag */
inline const long SYNC_STEP_RIVAL_APART = 100;  /* "other" = this far off */

/* Minimum ink on BOTH lines before a lag between them means anything: the
 * RMS of the mean-removed profile, in grey levels. A line that is mostly
 * blank paper with a little text cannot support the measurement - a few
 * marks line up about as well at several lags, and the winner is then
 * noise. This is the test that separates the real steps from the false
 * ones on the VMW recording, where the rival margin alone does not:
 *
 *   line   margin   ink (this line / previous)   what it is
 *    222    0.125       26.1 / 50.5              masthead: FALSE
 *    258    0.023       12.6 / 49.6              masthead: FALSE
 *    381    0.095        8.9 / 20.7              blank band: FALSE
 *   1032    0.000       89.7 / 119.8             dense picture: FALSE
 *    686    0.133       38.3 / 50.5              real dropout
 *    911    0.649       47.3 / 45.2              real dropout
 *    952    0.431       56.6 / 62.4              real dropout
 *
 * Both false steps in the masthead band shifted the top of the chart and
 * tore the Bureau's own logo in half; every real one is a lost 60-80 ms of
 * audio. */
inline const double SYNC_STEP_INK = 32.0;

/* Mean-removed copy of the line at `grid` rotated by `phi`, every `dec`th
 * sample averaged. `out` holds 4000/dec values. */
inline void sync_line_prof(const std::vector<int> &sm, long grid, long phi,
                           int dec, std::vector<double> &out)
{
    const long LINE = 4000;
    long m = LINE / dec;
    out.resize((size_t)m);
    double mean = 0.0;
    for (long i = 0; i < m; i++) {
        long s = 0;
        for (int k = 0; k < dec; k++)
            s += sm[grid + (i * dec + k + phi) % LINE];
        out[(size_t)i] = (double)s / dec;
        mean += out[(size_t)i];
    }
    mean /= (double)m;
    for (long i = 0; i < m; i++)
        out[(size_t)i] -= mean;
}

/* RMS of a mean-removed profile: how much ink the line carries. */
inline double sync_prof_ink(const std::vector<double> &a)
{
    double s = 0.0;
    for (size_t i = 0; i < a.size(); i++)
        s += a[i] * a[i];
    return a.empty() ? 0.0 : std::sqrt(s / (double)a.size());
}

/* Circular correlation of two mean-removed profiles at one lag, normalised
 * by their norms (so it is a correlation coefficient in [-1, 1]). */
inline double sync_prof_corr(const std::vector<double> &a,
                             const std::vector<double> &b, long lag)
{
    long m = (long)a.size();
    double s = 0.0, na = 0.0, nb = 0.0;
    for (long i = 0; i < m; i++) {
        double bv = b[(size_t)(((i - lag) % m + m) % m)];
        s += a[(size_t)i] * bv;
        na += a[(size_t)i] * a[(size_t)i];
        nb += bv * bv;
    }
    if (na < 1.0 || nb < 1.0)
        return 0.0;
    return s / (std::sqrt(na) * std::sqrt(nb));
}

/* Best lag of `a` against `b` within +-max_lag (in profile samples), the
 * correlation there, and the best RIVAL - the strongest peak that is
 * neither this one nor no-move at all. The peak has to win against that
 * rival, not merely be the largest number in the array: on a line that is
 * mostly white paper with a little text, several lags line the few marks
 * up about equally well and the winner is a coin toss. Measured on the VMW
 * recording, the peak-minus-rival margin at the three real dropouts is
 * 0.16 / 0.40 / 0.65, and at every false candidate 0.00-0.12 (two of them
 * in the masthead band, which they mangled). */
inline long sync_prof_peak(const std::vector<double> &a,
                           const std::vector<double> &b, long max_lag,
                           double *corr_out, double *rival_out)
{
    std::vector<double> cc((size_t)(2 * max_lag + 1));
    long best = 0;
    double bc = -2.0;
    for (long lag = -max_lag; lag <= max_lag; lag++) {
        double c = sync_prof_corr(a, b, lag);
        cc[(size_t)(lag + max_lag)] = c;
        if (c > bc) {
            bc = c;
            best = lag;
        }
    }
    if (corr_out)
        *corr_out = bc;
    if (rival_out) {
        double riv = -2.0;
        long apart = SYNC_STEP_RIVAL_APART / SYNC_STEP_DEC;
        for (long lag = -max_lag; lag <= max_lag; lag++) {
            long dp = lag - best < 0 ? best - lag : lag - best;
            long dz = lag < 0 ? -lag : lag;
            if (dp < apart || dz < apart)
                continue;
            double c = cc[(size_t)(lag + max_lag)];
            if (c > riv)
                riv = c;
        }
        *rival_out = riv;
    }
    return best;
}

/* The sideways step this line's content took against the previous line's,
 * in samples, or 0 to leave the phase alone. `phi` is the phase predicted
 * for this line (drift already applied). Needs the NEXT line's video too;
 * callers without it (end of stream) pass have_next = false and get 0.
 * Shared by scan_lines and LiveScan (invphasing-test). */
inline long sync_content_step(const std::vector<int> &sm, long grid,
                              long phi, bool have_next)
{
    const long LINE = 4000;
    if (grid < LINE || !have_next)
        return 0;
    if (grid + 2 * LINE > (long)sm.size())
        return 0;

    std::vector<double> prev, cur, next;
    sync_line_prof(sm, grid - LINE, phi, SYNC_STEP_DEC, prev);
    sync_line_prof(sm, grid, phi, SYNC_STEP_DEC, cur);
    if (sync_prof_ink(cur) < SYNC_STEP_INK ||
        sync_prof_ink(prev) < SYNC_STEP_INK)
        return 0;                      /* too little to measure with  */

    long span = SYNC_STEP_MAX_LAG / SYNC_STEP_DEC;
    double c = 0.0, rival = 0.0;
    long k = sync_prof_peak(cur, prev, span, &c, &rival);
    if (k * SYNC_STEP_DEC > -SYNC_STEP_MIN && k * SYNC_STEP_DEC < SYNC_STEP_MIN)
        return 0;                      /* the drift model's territory */
    if (c < SYNC_STEP_CORR)
        return 0;
    if (c - sync_prof_corr(cur, prev, 0) < SYNC_STEP_MARGIN)
        return 0;                      /* no better than not moving   */
    if (c - rival < SYNC_STEP_RIVAL)
        return 0;                      /* no better than other lags   */

    /* one line of lookahead: a real step persists */
    sync_line_prof(sm, grid + LINE, phi, SYNC_STEP_DEC, next);
    double c2 = 0.0;
    long k2 = sync_prof_peak(next, prev, span, &c2, nullptr);
    long d = (k2 - k) * SYNC_STEP_DEC;
    if (d < 0) d = -d;
    if (c2 < SYNC_STEP_CORR2 || d > SYNC_STEP_TOL)
        return 0;

    /* refine at full rate around the coarse peak */
    sync_line_prof(sm, grid - LINE, phi, 1, prev);
    sync_line_prof(sm, grid, phi, 1, cur);
    long base = k * SYNC_STEP_DEC;
    long best = base;
    double bc = -2.0;
    for (long lag = base - SYNC_STEP_DEC - 2;
         lag <= base + SYNC_STEP_DEC + 2; lag++) {
        double cc = sync_prof_corr(cur, prev, lag);
        if (cc > bc) {
            bc = cc;
            best = lag;
        }
    }
    return best;
}

/* Is the whole 4000-sample line starting at `grid` dark on average?
 * The phasing gate: WMO phasing lines are black-dominant (mean ~50 on the
 * VMW recording, ~5% white pulse), JMH lines and picture lines are not
 * (typically >150). This is what keeps the inverted path from ever firing
 * on a normal station or on picture content - including VMW's own dark
 * chart bands, whose lines still mean out above dark_th. */
inline bool sync_line_dark(const std::vector<int> &sm, long grid,
                           const SyncParams &p)
{
    const long LINE = 4000;
    if (grid < 0 || grid + LINE > (long)sm.size())
        return false;
    long s = 0;
    for (long i = grid; i < grid + LINE; i++)
        s += sm[i];
    return s / LINE < p.dark_th;
}

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
 *   - it must win clearly: EITHER the best rival at least 300 samples
 *     away is dark_th/2 brighter (a real sync pulse wins by a mile on a
 *     chart), OR the window is below sync_dark_floor outright - a
 *     satellite photo's near-black cloud rivals collapse the margin, so
 *     "genuinely black" is asked instead (the body's comment has the
 *     measurements), and
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
 * between one line and the next, ten times over the reception; the
 * 44.1 kHz sample does the same at line 930 where a new transmission
 * starts. Without this the decoder spends ten
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

/* ---- The two fallback searches, shared by scan_lines and LiveScan ----
 *
 * Both are pure functions of the smoothed video and a window, so the batch
 * and streaming paths call the very same code (they each held a copy until
 * these were shared). The callers differ only in how they pick [lo, hi]:
 * the whole line until the first lock of the stream, +-fallback_win/2
 * around the predicted edge afterwards. */

/* Our own second-chance search: the darkest position in [lo, hi], valid
 * when it is dark (dark_th) and dips at least fb_thresh below the window
 * mean. Deliberately NOT the original's test - that one slides a boxcar
 * over the binarised video and accepts the minimum MEAN when it falls
 * below SyncThre (docs/01 sec. 3.2(8), and sync_fallback_edge below is
 * it). Ours is a dip depth relative to the local mean, which is why
 * fb_thresh does not take SyncThre's value (DEVIATIONS.md #16). Tried
 * only where the ported tracker declines. */
inline long sync_fallback_search(const std::vector<int> &sm, long lo, long hi,
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
        sum += sm[(size_t)i];
        if (sm[(size_t)i] < sm[(size_t)best])
            best = i;
    }
    long mean = sum / (hi - lo + 1);
    if (sm[(size_t)best] < p.dark_th && mean - sm[(size_t)best] >= p.fb_thresh)
        return best;
    return -1;
}

/* The original's fallback tracker, docs/01 sec. 3.2(8), in OUR reference
 * convention. It slides a boxcar of fallback_win samples over the
 * binarised video and keeps the position with the minimum mean, but only
 * where the samples just outside the window are bright - i.e. the window
 * is a dark run with a bright edge, not merely a dark patch. Valid when
 * that minimum mean is below fb_mean, an absolute bound (SyncThre), and
 * when the move from the previous position is within search_win (MaxJump)
 * or is a wrap-around the long way.
 *
 * One deliberate departure: the original gates on the samples AFTER the
 * window and publishes the window's start, so its fallback anchors the
 * dark->bright edge while its own shape check anchors the bright pulse -
 * two reference points a whole fallback_win apart, which its own jump
 * guard then rejects (docs/01 sec. 3.2(8) "pick one reference point").
 * We gate on the samples BEFORE the window and publish its start, so the
 * raw position is the bright->dark edge - the same raw reference our
 * shape check starts from; sync_anchor() then refines both to the pulse's
 * darkest-window centre, so the phase is published at one consistent
 * reference. Same mechanism, one consistent reference. */
inline long sync_fallback_edge(const std::vector<int> &sm, long lo, long hi,
                               const SyncParams &p, long prev,
                               bool ever_locked)
{
    /* hard-coded in the original with no ini key (docs/01 sec. 3.2(8)) */
    const int FB_GATE = 8;          /* dword_4F25E4 */
    const int FB_GATE_LEVEL = 128;  /* dword_4F25E8 */
    const long LINE = 4000;

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
            gate += sm[(size_t)(q - i)] >= p.dark_th ? 255 : 0;
        if (gate / FB_GATE <= FB_GATE_LEVEL)
            continue;            /* no bright->dark edge here */
        int mean = 0;
        for (int i = 0; i < win; i++)
            mean += sm[(size_t)(q + i)] >= p.dark_th ? 255 : 0;
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
        if (d > p.search_win && d < LINE - p.search_win)
            return -1;           /* jumped further than MaxJump */
    }
    return best;
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
 * ~162 samples ten times - and rejecting costs more than it saves
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
