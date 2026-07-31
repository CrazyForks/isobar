/* rpm60-test - verifies the 60 rpm pipeline (docs/01 sec. 3.2(6)).
 *
 * No 60 rpm recording exists, so simulate one: time-stretch the
 * demodulated WEFAX video 2x (every sample twice) - that is exactly
 * what the same fax at 60 rpm looks like at 8000 S/s. Then:
 *   1. video_halve_rate(stretched) must equal the original video
 *      (pair average of duplicated samples is the identity);
 *   2. scanning the halved stream must give the same image as scanning
 *      the original directly.
 *
 * Sample-agnostic: prefers the full "jmh sample.wav" when present, else
 * the committed 30 s "jmh-sample-short.wav" excerpt.
 * Exit 0 on pass, 1 on fail.
 */
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/syncscan.h"

#include <cstdio>

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
    fprintf(stderr, "video: %zu samples (%.1f s @ 8000 S/s)\n",
            video.size(), video.size() / 8000.0);

    std::vector<uint8_t> stretched;
    stretched.reserve(video.size() * 2);
    for (uint8_t b : video) {
        stretched.push_back(b);
        stretched.push_back(b);
    }

    std::vector<uint8_t> halved = video_halve_rate(stretched);
    if (halved != video) {
        fprintf(stderr, "FAIL: halve_rate(dup(v)) != v\n");
        return 1;
    }

    FaxImage a = scan_lines(video);
    FaxImage b = scan_lines(halved);
    if (a.lines.size() != b.lines.size()) {
        fprintf(stderr, "FAIL: line counts differ (%zu vs %zu)\n",
                a.lines.size(), b.lines.size());
        return 1;
    }
    for (size_t i = 0; i < a.lines.size(); i++) {
        if (a.lines[i] != b.lines[i]) {
            fprintf(stderr, "FAIL: line %zu differs\n", i);
            return 1;
        }
    }
    printf("OK: 60 rpm path reproduces the 120 rpm image "
           "(%zu lines, %d locked, %d coasted)\n",
           b.lines.size(), b.lines_locked, b.lines_coasted);
    return 0;
}
