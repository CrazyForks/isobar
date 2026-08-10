/* syncscan.cpp - see syncscan.h. */

#include "syncscan.h"
#include "live.h"    /* scan_lines is one pass of LiveScan - see below */
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

/* The batch entry point: one pass of the streaming scanner over a
 * finished buffer.
 *
 * There is ONE sync state machine - LiveScan - and two ways into it,
 * exactly as decoder.cpp has one FmDecoder class and a batch fm_decode()
 * loop over it. Until v1.8.0 this file carried a second, independent
 * implementation of the same algorithm: its own moving average, its own
 * edge collectors, its own lock acquisition and its own per-line state
 * machine, ~500 lines duplicating core/live.cpp. The two were kept in
 * step by hand and by cli/live-test.cpp, and they did drift - S37 had to
 * fix the same missing gate twice, once on each side.
 *
 * Feeding the whole buffer in one call means none of LiveScan's "wait for
 * more samples" paths can fire (every horizon it tests is already behind
 * buf.size()), so this runs as a single forward pass, like the scanner it
 * replaces. finish() then releases the line pump() holds back for
 * sync_step_lock's one-line lookahead - the same line the old batch code
 * emitted by simply running off the end of the file.
 *
 * Verified byte-identical on all eleven recordings before the switch:
 * every line, every pixel, and all four counters. The one difference the
 * comparison did find was a live BUG, not a batch one - a stream ending
 * while unlocked lost its last line - and it is fixed in live.cpp.
 *
 * cli/live-test.cpp still earns its place: feeding the same video in
 * chunks (including sizes that split sync pulses and lines) must give
 * what this single-shot feed gives, so it now pins chunking invariance -
 * the property that actually matters for live audio - rather than the
 * agreement of two hand-synchronised copies. */
namespace {

struct BatchCollect {
    FaxImage *img;
    void (*line_state)(int);
};

void batch_line(const uint8_t *line, int state, void *ud)
{
    BatchCollect *c = (BatchCollect *)ud;
    c->img->lines.push_back(
        std::vector<uint8_t>(line, line + FaxImage::WIDTH));
    if (c->line_state)
        c->line_state(state);
}

} /* namespace */

FaxImage scan_lines(const std::vector<uint8_t> &video,
                    void (*line_state)(int),
                    const SyncParams *params,
                    bool track_enable)
{
    FaxImage img;

    BatchCollect c;
    c.img = &img;
    c.line_state = line_state;

    LiveScan ls(params ? *params : sync_default_params());
    ls.set_track(track_enable);
    ls.feed(video.data(), video.size(), batch_line, &c);
    ls.finish(batch_line, &c);

    img.lines_locked    = ls.lines_locked;
    img.lines_corrected = ls.lines_corrected;
    img.lines_coasted   = ls.lines_coasted;
    img.relocks         = ls.relocks;
    return img;
}
