/* live-test - verifies LiveScan (streaming) against scan_lines (batch).
 *
 * Feeds the demodulated WEFAX video through LiveScan in chunks (several
 * chunk sizes, incl. odd ones that split sync pulses and lines) and
 * collects the emitted lines. The result must match the batch decode
 * line for line, byte for byte, with equal stats.
 *
 * Sample-agnostic: prefers the full "jmh sample.wav" when present, else
 * the committed 30 s "jmh-sample-short.wav" excerpt.
 * Exit 0 on pass, 1 on fail.
 */
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/live.h"

#include <cstdio>

struct Collect {
    FaxImage img;
    int      states[3] = {0, 0, 0};
};

static void on_line(const uint8_t *line, int state, void *ud)
{
    Collect *c = (Collect *)ud;
    c->img.lines.push_back(std::vector<uint8_t>(line, line + FaxImage::WIDTH));
    if (state >= 0 && state <= 2)
        c->states[state]++;
}

static int run_case(const std::vector<uint8_t> &video, size_t chunk,
                    const FaxImage &ref, bool async_finish)
{
    LiveScan ls;
    Collect c;
    for (size_t off = 0; off < video.size(); off += chunk) {
        size_t n = video.size() - off;
        if (n > chunk)
            n = chunk;
        ls.feed(video.data() + off, n, on_line, &c);
    }
    /* sync_step_lock needs one line of lookahead, so the last line is
     * held back until the stream is declared over (core/live.h).
     * Both ways of declaring it must give the same result: the direct
     * call, and the UI-thread form where request_finish() posts and the
     * next feed() performs it - which is what the GUI's record_off()
     * relies on to keep a reception's last line. */
    if (async_finish) {
        ls.request_finish();
        if (ls.finish_done()) {
            fprintf(stderr, "  request_finish reported done before any "
                            "feed() could perform it\n");
            return 1;
        }
        ls.feed(video.data(), 0, on_line, &c);   /* empty buffer: flush */
        if (!ls.finish_done()) {
            fprintf(stderr, "  feed() did not perform the posted flush\n");
            return 1;
        }
    } else {
        ls.finish(on_line, &c);
    }

    bool ok = c.img.lines.size() == ref.lines.size() &&
              ls.lines_locked == ref.lines_locked &&
              ls.lines_corrected == ref.lines_corrected &&
              ls.lines_coasted == ref.lines_coasted &&
              ls.relocks == ref.relocks;
    size_t first_bad = 0;
    if (ok) {
        for (size_t i = 0; i < ref.lines.size(); i++) {
            if (c.img.lines[i] != ref.lines[i]) {
                ok = false;
                first_bad = i;
                break;
            }
        }
    }
    fprintf(stderr, "chunk %5zu %-6s: %4zu lines (L%d C%d P%d R%d) %s\n",
            chunk, async_finish ? "async" : "direct",
            c.img.lines.size(), ls.lines_locked.load(),
            ls.lines_corrected.load(), ls.lines_coasted.load(),
            ls.relocks.load(),
            ok ? "OK" : "MISMATCH");
    if (!ok) {
        fprintf(stderr, "  ref: %4zu lines (L%d C%d P%d R%d), first bad "
                "line %zu\n",
                ref.lines.size(), ref.lines_locked, ref.lines_corrected,
                ref.lines_coasted, ref.relocks, first_bad);
        return 1;
    }
    return 0;
}

/* try the full local sample first, then the committed excerpt */
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
    FaxImage ref = scan_lines(video);
    fprintf(stderr, "batch: %zu lines (L%d C%d P%d R%d)\n",
            ref.lines.size(), ref.lines_locked, ref.lines_corrected,
            ref.lines_coasted, ref.relocks);

    const size_t chunks[] = {800, 4000, 7777, 65536, 1000000};
    int fails = 0;
    for (size_t chunk : chunks)
        fails += run_case(video, chunk, ref, false);
    /* the posted-flush path must land in exactly the same place */
    for (size_t chunk : chunks)
        fails += run_case(video, chunk, ref, true);

    if (fails) {
        fprintf(stderr, "FAIL: %d case(s) mismatched\n", fails);
        return 1;
    }
    printf("OK: streaming assembly identical to batch\n");
    return 0;
}
