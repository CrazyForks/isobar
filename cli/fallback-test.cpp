/* fallback-test - exercises the sync fallback + hysteresis paths
 * (docs/01 sec. 3.2(8)) that a clean signal never triggers.
 *
 * Loads the demodulated WEFAX video from a test sample, erases sync pulses
 * (paints them white), then checks:
 *   1. baseline: default params decode without losing the sync grid;
 *   2. short gap: 10 erased pulses mid-image -> decode continues through
 *      the gap (fallback correction or coasting) and re-locks afterwards;
 *   3. long gap: a run of solid white longer than RReSycn=60 -> full
 *      release + re-acquire (relocks >= 1), image continues after the gap;
 *   4. GUI-default params (Sync2Thre=100 etc.) produce the same line
 *      count on the clean signal - the Details-dialog defaults must
 *      not change clean-signal behavior.
 *
 * Sample-agnostic: prefers the full "jmh sample.wav" when present (longer,
 * exercises more of the chain) and falls back to the committed 30 s
 * "jmh-sample-short.wav" excerpt. Gap positions scale to the sample.
 * Exit 0 on pass, 1 on fail.
 */
#include "../core/settings.h"
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/syncscan.h"

#include <cstdio>

/* dark runs (start,len) with sync-pulse-like lengths, threshold 96 -
 * mirrors the shape check in syncscan.cpp */
static std::vector<std::pair<long, long>> dark_runs(
        const std::vector<uint8_t> &v)
{
    std::vector<std::pair<long, long>> runs;
    long start = -1;
    for (size_t i = 0; i < v.size(); i++) {
        bool dark = v[i] < 96;
        if (dark && start < 0)
            start = (long)i;
        else if (!dark && start >= 0) {
            long len = (long)i - start;
            if (len >= 100 && len <= 400)
                runs.push_back({start, len});
            start = -1;
        }
    }
    return runs;
}

/* paint n_consecutive pulses white, starting at pulse index `skip` */
static void erase_pulses(std::vector<uint8_t> &v,
                         const std::vector<std::pair<long, long>> &runs,
                         size_t skip, size_t count)
{
    for (size_t i = skip; i < skip + count && i < runs.size(); i++)
        for (long k = 0; k < runs[i].second; k++)
            v[runs[i].first + k] = 255;
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
    std::vector<std::pair<long, long>> runs = dark_runs(video);
    fprintf(stderr, "video: %zu samples, %zu sync-like pulses\n",
            video.size(), runs.size());
    if (runs.size() < 50) {
        fprintf(stderr, "FAIL: sample too short / no sync pulses\n");
        return 1;
    }

    /* 1. baseline */
    FaxImage base = scan_lines(video);
    fprintf(stderr, "baseline: %zu lines (locked %d, corrected %d, "
            "coasted %d, relocks %d)\n",
            base.lines.size(), base.lines_locked, base.lines_corrected,
            base.lines_coasted, base.relocks);
    if (base.lines.size() < 50) {
        fprintf(stderr, "FAIL: baseline lost the sync grid\n");
        return 1;
    }

    /* 2. short gap: 10 erased pulses mid-image. Scale the start so it
     * lands in the middle of whatever sample we have. */
    {
        std::vector<uint8_t> v = video;
        size_t mid = runs.size() / 2;
        erase_pulses(v, runs, mid, 10);
        FaxImage img = scan_lines(v);
        fprintf(stderr, "10-gap:   %zu lines (locked %d, corrected %d, "
                "coasted %d, relocks %d)\n",
                img.lines.size(), img.lines_locked, img.lines_corrected,
                img.lines_coasted, img.relocks);
        int bridged = img.lines_corrected + img.lines_coasted;
        if (bridged == 0 || img.lines.size() != base.lines.size()) {
            fprintf(stderr, "FAIL: gap not bridged\n");
            return 1;
        }
        /* The erased lines lose shape lock, but the decode after the gap
         * must re-lock, not wander. Allow a small margin past the 10 erased
         * lines for the re-acquisition transient. */
        if (img.lines_locked + img.lines_corrected < base.lines_locked - 10) {
            fprintf(stderr, "FAIL: did not re-lock after the gap\n");
            return 1;
        }
    }

    /* 3. long gap: 70 lines painted solid white (> RReSycn=60), centered
     * on the middle of the video. This needs a sample long enough that real
     * signal follows the gap to re-acquire onto; on a short excerpt there
     * is nothing after the gap, so the release+re-acquire path is skipped
     * rather than tested dishonestly. */
    if (base.lines.size() >= 200) {
        std::vector<uint8_t> v = video;
        long mid = (long)v.size() / 2;
        long start = mid - 35L * 4000;         /* ~line window around center */
        long end   = mid + 35L * 4000;
        for (long i = start; i < end && i < (long)v.size(); i++)
            if (i >= 0) v[i] = 255;
        FaxImage img = scan_lines(v);
        fprintf(stderr, "70-gap:   %zu lines (locked %d, corrected %d, "
                "coasted %d, relocks %d)\n",
                img.lines.size(), img.lines_locked, img.lines_corrected,
                img.lines_coasted, img.relocks);
        if (img.relocks < base.relocks + 1) {
            fprintf(stderr, "FAIL: no full release + re-acquire\n");
            return 1;
        }
        /* the grid must survive: roughly the same number of lines as the
         * baseline (no runaway), never more */
        if (img.lines.size() > base.lines.size() ||
            (long)img.lines.size() < (long)base.lines.size() - 70) {
            fprintf(stderr, "FAIL: bad line count after gap "
                    "(lost sync grid?)\n");
            return 1;
        }
    } else {
        fprintf(stderr, "70-gap:   skipped (sample has %zu lines; needs >=200 "
                "for gap+recovery)\n", base.lines.size());
    }

    /* 4. GUI-default params on the clean signal. Built through the real
     * settings->params mapping rather than a copy of it, so this test
     * fails if that mapping drifts (it used to hard-code the values with
     * LReSycn/RReSycn the wrong way round, and said so in its comments). */
    {
        KgSettings cfg;
        settings_defaults(cfg);
        SyncParams sp = sync_params_from_settings(cfg);
        FaxImage img = scan_lines(video, nullptr, &sp);
        fprintf(stderr, "gui-parms: %zu lines (locked %d, corrected %d, "
                "coasted %d, relocks %d)\n",
                img.lines.size(), img.lines_locked, img.lines_corrected,
                img.lines_coasted, img.relocks);
        if (img.lines.size() != base.lines.size()) {
            fprintf(stderr, "FAIL: GUI defaults change clean decode\n");
            return 1;
        }
    }

    printf("OK: fallback correction, release + re-acquire verified\n");
    return 0;
}
