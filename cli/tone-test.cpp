/* tone-test - verifies 300/450 Hz tone detection (docs/01 sec. 3.2(5)).
 *
 * Synthetic video: 2 s of 300 Hz amplitude oscillation, 2 s flat,
 * 2 s of 450 Hz. The 300 Hz level must pass the threshold only in the
 * first segment, the 450 Hz level only in the last; flat must stay
 * quiet; the two must not cross-detect.
 *
 * (An earlier version also checked a real-recording section against the
 * full JMH sample's known tone timestamps. That was dropped when the
 * committed test fixture was trimmed to a 30 s excerpt (jmh-sample-short.wav,
 * t=60-90s of the original) which contains no start/stop tones — the
 * synthetic section above is what validates the detector; the real tones
 * were a bonus integration check tied to one recording's timestamps.)
 * Exit 0 on pass, 1 on fail.
 */
#include "../core/tonedetect.h"

#include <cmath>
#include <cstdio>

/* Local PI — see core/tonedetect.cpp (M_PI is missing under MSVC). */
static const double PI = 3.14159265358979323846;

static double run_segment(ToneDetect &td, double freq, int seconds,
                          double &other_level)
{
    double lv3 = 0, lv4 = 0, l3 = 0, l4 = 0;
    for (int i = 0; i < seconds * 8000; i++) {
        unsigned char v = freq > 0
            ? (unsigned char)(128 + 120 * sin(2.0 * PI * freq * i / 8000.0))
            : 128;
        if (td.feed(v, l3, l4)) {
            lv3 = l3;   /* last block of the segment */
            lv4 = l4;
        }
    }
    other_level = lv4;
    return lv3;
}

int main()
{
    ToneDetect td;
    double other;
    double l300_in_300 = run_segment(td, 300, 2, other);
    double l450_in_300 = other;
    double l300_in_flat = run_segment(td, 0, 2, other);
    double l450_in_flat = other;
    double l300_in_450 = run_segment(td, 450, 2, other);
    double l450_in_450 = other;

    fprintf(stderr,
            "synthetic: 300Hz seg -> 300:%.3g 450:%.3g | "
            "flat -> 300:%.3g 450:%.3g | 450Hz seg -> 300:%.3g 450:%.3g\n",
            l300_in_300, l450_in_300, l300_in_flat, l450_in_flat,
            l300_in_450, l450_in_450);

    if (l300_in_300 < ToneDetect::LEVEL_TH ||
        l450_in_450 < ToneDetect::LEVEL_TH) {
        fprintf(stderr, "FAIL: tones not detected\n");
        return 1;
    }
    if (l300_in_flat > ToneDetect::LEVEL_TH / 10 ||
        l450_in_flat > ToneDetect::LEVEL_TH / 10) {
        fprintf(stderr, "FAIL: false detection on flat signal\n");
        return 1;
    }
    if (l450_in_300 > ToneDetect::LEVEL_TH ||
        l300_in_450 > ToneDetect::LEVEL_TH) {
        fprintf(stderr, "FAIL: cross-detection between 300/450 Hz\n");
        return 1;
    }

    printf("OK: tone detection verified\n");
    return 0;
}
