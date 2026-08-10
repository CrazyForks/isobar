/* invphasing-test - verifies the WMO inverted-phasing path
 * (DEVIATIONS.md #19): white-pulse-in-black acquisition + phase hold,
 * on the committed "vmw-phasing-12k.wav" fixture (62 s cut from an
 * off-air VMW/Wiluna recording: ~7 s white carrier, ~30 s phasing,
 * ~25 s picture head, 12 kHz 16-bit mono).
 *
 * Checks:
 *   1. batch: the decoder locks from the inverted phasing (locked lines
 *      exist), then HOLDS through the picture - no fallback corrections
 *      after the first locked line (the wander this feature replaces
 *      had 133 false corrections on the full recording), coasted lines
 *      cover the picture, and the held phase at the last line is the
 *      golden value measured on the reference decode;
 *   2. live: LiveScan fed in chunks produces the same lines byte for
 *      byte with equal stats (direct and posted finish), as live-test
 *      does for the normal path.
 * Exit 0 on pass, 1 on fail.
 */
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/live.h"

#include <cstdio>
#include <vector>

struct Collect {
    std::vector<std::vector<uint8_t>> lines;
    std::vector<int> state;
};
static void on_line(const uint8_t *line, int state, void *ud)
{
    Collect *c = (Collect *)ud;
    c->lines.push_back(std::vector<uint8_t>(line, line + FaxImage::WIDTH));
    c->state.push_back(state);
}

static std::vector<int> batch_states;
static void on_state(int s) { batch_states.push_back(s); }

/* Recover the rotation the decoder used for line n (same method as
 * manual-sync-test): the phi that reproduces the line byte for byte. */
static long recover_phi(const std::vector<uint8_t> &video,
                        const std::vector<uint8_t> &line, size_t n)
{
    long grid = (long)n * 4000;
    for (long phi = 0; phi < 4000; phi++) {
        bool ok = true;
        for (int i = 0; i < FaxImage::WIDTH; i++)
            if (line[i] != video[grid + (8L * i / 3 + phi) % 4000]) {
                ok = false;
                break;
            }
        if (ok)
            return phi;
    }
    return -1;
}

int main()
{
    std::vector<double> audio = wav_read_22050("vmw-phasing-12k.wav", 0);
    std::vector<uint8_t> video = fm_decode(audio, 0);

    FaxImage ref = scan_lines(video, on_state);
    fprintf(stderr, "batch: %zu lines (L%d C%d P%d R%d)\n", ref.lines.size(),
            ref.lines_locked, ref.lines_corrected, ref.lines_coasted,
            ref.relocks);

    int fails = 0;

    /* 124 lines = 62 s at 120 rpm */
    if (ref.lines.size() != 124) {
        fprintf(stderr, "FAIL: %zu lines, expected 124\n", ref.lines.size());
        fails++;
    }

    /* locked from the inverted phasing: the 30 s preamble gives ~55
     * shape/re-anchor lines; ask for a clear margin */
    if (ref.lines_locked < 30) {
        fprintf(stderr, "FAIL: only %d locked lines - no inverted "
                "acquisition\n", ref.lines_locked);
        fails++;
    }

    /* the picture is held, not tracked: once the first locked line has
     * passed, no line may be a fallback correction */
    size_t first_lock = 0;
    while (first_lock < batch_states.size() && batch_states[first_lock] != 0)
        first_lock++;
    int corr_after = 0;
    for (size_t i = first_lock; i < batch_states.size(); i++)
        if (batch_states[i] == 1)
            corr_after++;
    if (corr_after != 0) {
        fprintf(stderr, "FAIL: %d corrected lines after the first lock "
                "(line %zu) - the phase is wandering, not held\n",
                corr_after, first_lock);
        fails++;
    }

    /* the picture coasts */
    if (ref.lines_coasted < 40) {
        fprintf(stderr, "FAIL: only %d coasted lines - picture not held\n",
                ref.lines_coasted);
        fails++;
    }

    /* held phase at the last line. Golden value: recovered from the
     * reference decode of this fixture (SYNC_INV_OFFSET included). */
    long phi = recover_phi(video, ref.lines[123], 123);
    if (phi != 3779) {
        fprintf(stderr, "FAIL: held phase %ld at the last line, golden 3779\n",
                phi);
        fails++;
    }

    /* the hold runs at the MEASURED line period, not at the nominal 4000
     * (DEVIATIONS.md #19). This receiver's 12 kHz stream is really
     * ~12000.96 Hz = 3999.68 samples per line, so a correct hold walks
     * the phase back by ~0.32 samples per line - 21 over the 63 picture
     * lines here. Held at exactly 4000 the phase would not move at all,
     * and the full chart came out sheared by 154 px. */
    long phi60 = recover_phi(video, ref.lines[60], 60);
    long drift = phi60 - phi;
    if (drift < 18 || drift > 24) {
        fprintf(stderr, "FAIL: phase moved %ld samples from line 60 to 123 "
                "(expected ~21: the fitted period, not 4000)\n", drift);
        fails++;
    }

    /* A chart's own dark bands must NOT re-anchor the phase: they look
     * exactly like phasing to a per-line test, and on the full recording
     * two of them (2 and 4 lines long) walked the image sideways. Only a
     * run of SYNC_PHASING_CONFIRM black-dominant lines counts as a
     * preamble. Injected here because the 62 s fixture has no such band:
     * `run` black lines carrying a white pulse search_win-18 samples off
     * the held phase - close enough that the re-anchor would take it. */
    for (int run : {4, 16}) {
        std::vector<uint8_t> v2 = video;
        long first = 100, last = first + run;
        for (long s = first * 4000 - 200; s < last * 4000; s++)
            v2[(size_t)s] = 20;
        for (long n = first; n < last; n++) {
            long ph = recover_phi(video, ref.lines[(size_t)n], (size_t)n);
            long a = n * 4000 + ph + 18;      /* decoy anchor, +18 off  */
            long end = a - SYNC_INV_OFFSET;   /* ... as a pulse        */
            for (long s = end - 190; s < end; s++)
                v2[(size_t)s] = 250;
        }
        FaxImage got = scan_lines(v2);
        long probe = last + 5;
        long moved = recover_phi(v2, got.lines[(size_t)probe], (size_t)probe) -
                     recover_phi(video, ref.lines[(size_t)probe],
                                 (size_t)probe);
        bool want_move = run >= SYNC_PHASING_CONFIRM;
        if ((moved != 0) != want_move) {
            fprintf(stderr, "FAIL: %d dark lines moved the phase by %ld, "
                    "expected %s\n", run, moved,
                    want_move ? "a re-anchor" : "nothing");
            fails++;
        }
    }

    /* A dropout mid-picture must be followed off the picture's own
     * content (DEVIATIONS.md #19): with no per-line pulse there is
     * nothing else to notice it with, and every sample lost moves the
     * rest of the chart sideways for good. The real recording loses
     * 60-80 ms three times; here 600 samples (75 ms) are spliced out at
     * line 100, and the decoder must take the phase back by exactly
     * that much. */
    {
        std::vector<uint8_t> v2(video.begin(), video.begin() + 100 * 4000);
        v2.insert(v2.end(), video.begin() + 100 * 4000 + 600, video.end());
        FaxImage got = scan_lines(v2);
        long want = (recover_phi(video, ref.lines[110], 110) - 600 + 4000)
                    % 4000;
        long have = recover_phi(v2, got.lines[110], 110);
        long d = have - want;
        if (d > 2000) d -= 4000;
        if (d < -2000) d += 4000;
        if (d < -4 || d > 4) {
            fprintf(stderr, "FAIL: 600-sample dropout left the phase at %ld, "
                    "expected %ld (+-4)\n", have, want);
            fails++;
        }
        if (got.relocks != ref.relocks + 1) {
            fprintf(stderr, "FAIL: %d relocks after the dropout, expected "
                    "%d\n", got.relocks, ref.relocks + 1);
            fails++;
        }
    }

    /* live must equal batch byte for byte (direct + posted finish) */
    const size_t chunks[] = {800, 65536};
    for (int async = 0; async <= 1; async++)
        for (size_t chunk : chunks) {
            LiveScan ls;
            Collect c;
            for (size_t off = 0; off < video.size(); off += chunk) {
                size_t n = video.size() - off;
                if (n > chunk)
                    n = chunk;
                ls.feed(video.data() + off, n, on_line, &c);
            }
            if (async) {
                ls.request_finish();
                ls.feed(video.data(), 0, on_line, &c);
            } else {
                ls.finish(on_line, &c);
            }
            bool ok = c.lines.size() == ref.lines.size() &&
                      ls.lines_locked == ref.lines_locked &&
                      ls.lines_corrected == ref.lines_corrected &&
                      ls.lines_coasted == ref.lines_coasted &&
                      ls.relocks == ref.relocks;
            if (ok)
                for (size_t i = 0; i < ref.lines.size(); i++)
                    if (c.lines[i] != ref.lines[i]) {
                        ok = false;
                        break;
                    }
            fprintf(stderr, "live chunk %5zu %-6s: %s\n", chunk,
                    async ? "async" : "direct", ok ? "OK" : "MISMATCH");
            if (!ok) {
                fprintf(stderr, "  live %zu lines (L%d C%d P%d R%d) vs "
                        "batch %zu (L%d C%d P%d R%d)\n", c.lines.size(),
                        ls.lines_locked.load(), ls.lines_corrected.load(),
                        ls.lines_coasted.load(), ls.relocks.load(),
                        ref.lines.size(), ref.lines_locked,
                        ref.lines_corrected, ref.lines_coasted, ref.relocks);
                fails++;
            }
        }

    if (fails) {
        fprintf(stderr, "FAIL: %d case(s)\n", fails);
        return 1;
    }
    printf("OK: inverted phasing acquires, holds, and matches live\n");
    return 0;
}
