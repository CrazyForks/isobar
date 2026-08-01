/* manual-sync-test - verifies LiveScan::nudge_phase (manual sync
 * position, docs/01 sec. 3.2 "Sync-track enable" + sec. 4 "Zoom/pan";
 * the original readme's 同期処理の停止と手動同期位置指定).
 *
 * Case 1 - the phase math is exact. With tracking OFF nothing else
 * touches the phase, so a nudge of 8*k samples must shift every later
 * line by exactly k taps and nothing more. Lines are built as
 *     px[i] = video[grid + (8*i/3 + phi) % 4000]
 * and 8*(i+3k)/3 == 8*i/3 + 8*k exactly in integer arithmetic (24*k is
 * a multiple of 3), so a nudged line must equal the un-nudged one read
 * 3*k pixels further along, wrapping at 1500:
 *     nudged[i] == plain[(i + 3*k) % 1500]
 * Checked for a positive nudge, a negative one, and one past a whole
 * line (the wrap into [0,4000) must make those three agree).
 *
 * Case 2 - tracking resumes FROM the seeded position instead of
 * re-acquiring. The point of the feature is that the decoder searches
 * around where you pointed even when that is NOT where the strongest
 * sync-looking dip is; a re-acquisition from scratch would walk off to
 * the dip and throw your position away. So: seed the phase half a line
 * away from where automatic tracking puts it, then recover the phase
 * the decoder actually used on the next line and require it to still
 * be inside the narrow search window around the seed. The line must
 * also be reported locked or corrected, never "not locked yet".
 * (Only the first line after the nudge is checked - later relocks over
 * a 10-minute recording are ordinary decoder behavior, not this
 * feature.)
 *
 * Sample-agnostic: prefers the full "jmh sample.wav", else the
 * committed 30 s excerpt. Exit 0 on pass, 1 on fail.
 */
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/live.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

static const long LINE = 4000;   /* samples per 120-rpm line */
static const int  SPLIT = 12;    /* lines fed before the nudge */

struct Collect {
    std::vector<std::vector<uint8_t>> lines;
    std::vector<int> state;   /* per line: 0 lock 1 corr 2 coast 3 none */
};

static void on_line(const uint8_t *line, int state, void *ud)
{
    Collect *c = (Collect *)ud;
    c->lines.push_back(std::vector<uint8_t>(line, line + FaxImage::WIDTH));
    c->state.push_back(state);
}

/* Feed the video in two halves, split on a line boundary so whatever
 * happens between them lands between lines SPLIT-1 and SPLIT.
 * Tracking starts OFF (the state the feature is used from).
 * act 0 = nothing; 1 = nudge, then freeze again (isolates the phase
 * math); 2 = nudge and let tracking run on, as in real use; 3 =
 * ordinary automatic tracking from the start, for reference. */
static Collect run(const std::vector<uint8_t> &video, long delta, int act)
{
    LiveScan ls;
    Collect c;
    ls.set_track(act == 3);
    size_t half = (size_t)(SPLIT * LINE);
    if (half > video.size())
        half = video.size();
    ls.feed(video.data(), half, on_line, &c);
    if (act == 1) {
        ls.nudge_phase(delta);
        ls.set_track(false);
    } else if (act == 2) {
        ls.nudge_phase(delta);
    }
    ls.feed(video.data() + half, video.size() - half, on_line, &c);
    return c;
}

static int check_shift(const Collect &plain, const Collect &nudged,
                       int k, const char *what)
{
    if (plain.lines.size() != nudged.lines.size()) {
        fprintf(stderr, "%s: %zu lines vs %zu\n", what,
                nudged.lines.size(), plain.lines.size());
        return 1;
    }
    for (size_t n = SPLIT; n < plain.lines.size(); n++)
        for (int i = 0; i < FaxImage::WIDTH; i++) {
            int j = ((i + 3 * k) % FaxImage::WIDTH + FaxImage::WIDTH) %
                    FaxImage::WIDTH;
            if (nudged.lines[n][i] != plain.lines[n][j]) {
                fprintf(stderr, "%s: line %zu px %d: %u, expected %u\n",
                        what, n, i, nudged.lines[n][i], plain.lines[n][j]);
                return 1;
            }
        }
    /* the lines before the nudge must be untouched */
    for (size_t n = 0; n < SPLIT && n < plain.lines.size(); n++)
        if (nudged.lines[n] != plain.lines[n]) {
            fprintf(stderr, "%s: line %zu changed before the nudge\n",
                    what, n);
            return 1;
        }
    fprintf(stderr, "%s: OK (%zu lines shifted by %d taps)\n", what,
            plain.lines.size() - SPLIT, k);
    return 0;
}

/* Recover the rotation the decoder used for line n. Lines are built as
 * video[grid + (8*i/3 + phi) % 4000], so the phi that reproduces the
 * line byte for byte is the one the decoder held. -1 = no match. */
static long recover_phi(const std::vector<uint8_t> &video,
                        const std::vector<uint8_t> &line, size_t n)
{
    long grid = (long)n * LINE;
    for (long phi = 0; phi < LINE; phi++) {
        bool ok = true;
        for (int i = 0; i < FaxImage::WIDTH; i++)
            if (line[i] != video[grid + (8L * i / 3 + phi) % LINE]) {
                ok = false;
                break;
            }
        if (ok)
            return phi;
    }
    return -1;
}

/* distance the short way round a 4000-sample line */
static long circ_dist(long a, long b)
{
    long d = labs(a - b) % LINE;
    return d > LINE / 2 ? LINE - d : d;
}

static std::vector<double> load_audio()
{
    try { return wav_read_22050("jmh sample.wav", 0); }
    catch (...) {}
    return wav_read_22050("jmh-sample-short.wav", 0);
}

int main()
{
    std::vector<double> audio = load_audio();
    std::vector<uint8_t> video = fm_decode(audio, 0);
    if ((long)video.size() < (SPLIT + 4) * LINE) {
        fprintf(stderr, "FAIL: sample too short (%zu samples)\n",
                video.size());
        return 1;
    }

    /* Case 1: tracking off throughout, so only the nudge moves phi. */
    Collect plain = run(video, 0, 0);
    fprintf(stderr, "reference: %zu lines, tracking off\n",
            plain.lines.size());

    const int k = 137;              /* taps; 8*k samples */
    int fails = 0;
    fails += check_shift(plain, run(video, 8L * k, 1), k,
                         "positive nudge");
    /* 8*k - 4000 is the same rotation once wrapped into [0,4000) */
    fails += check_shift(plain, run(video, 8L * k - LINE, 1), k,
                         "negative nudge");
    /* one whole line further round changes nothing */
    fails += check_shift(plain, run(video, 8L * k + LINE, 1), k,
                         "nudge past a line");

    /* Case 2: the ordinary use - Sync released, click, tracking
     * resumes from the clicked position. Seed half a line away from
     * where automatic tracking sits, so "stayed near the seed" and
     * "re-acquired the real sync" are far apart and cannot be
     * confused. */
    Collect autorun = run(video, 0, 3);
    long phi_auto = recover_phi(video, autorun.lines[SPLIT], SPLIT);
    if (phi_auto < 0) {
        fprintf(stderr, "FAIL: could not recover the automatic phase\n");
        return 1;
    }
    long seed = ((phi_auto + LINE / 2) / 8 * 8) % LINE;   /* multiple of 8 */
    Collect resumed = run(video, seed, 2);
    long phi_man = recover_phi(video, resumed.lines[SPLIT], SPLIT);
    /* the widest the decoder may move in one line: the shape-match
     * window, or half the fallback window (core/syncscan.h) */
    SyncParams sp = sync_default_params();
    long win = sp.search_win > sp.fallback_win / 2 ? sp.search_win
                                                   : sp.fallback_win / 2;
    fprintf(stderr, "automatic phase %ld, seeded %ld, after the nudge %ld "
            "(state %d)\n", phi_auto, seed, phi_man, resumed.state[SPLIT]);
    if (phi_man < 0) {
        fprintf(stderr, "FAIL: could not recover the phase after the "
                "nudge\n");
        fails++;
    } else if (circ_dist(phi_man, seed) > win) {
        fprintf(stderr, "FAIL: phase moved %ld samples off the seeded "
                "position (window %ld) - the manual position was "
                "discarded, %ld from the automatic one\n",
                circ_dist(phi_man, seed), win,
                circ_dist(phi_man, phi_auto));
        fails++;
    }
    if (resumed.state[SPLIT] != 0 && resumed.state[SPLIT] != 1) {
        fprintf(stderr, "FAIL: line after the nudge is state %d, expected "
                "locked (0) or corrected (1)\n", resumed.state[SPLIT]);
        fails++;
    }

    if (fails) {
        fprintf(stderr, "FAIL: %d case(s)\n", fails);
        return 1;
    }
    printf("OK: manual sync align shifts the phase exactly and resumes "
           "tracking\n");
    return 0;
}
