/* ratefit-test - the clock-error measurement (core/ratefit.h).
 *
 * The round trip is the test: take a real reception, retime it by a KNOWN
 * clock error, and check the fit measures that error back and that
 * applying it returns the stream to 4000. Real video rather than a
 * synthetic pattern is the point - a synthetic chart would have exactly
 * the vertical features the fold likes, and so would not say whether a
 * weather chart has enough of them.
 *
 * Fixture: jmh-offair-12k.wav, 60 s of off-air JMH. JMH is the one
 * station here that sends a per-line sync strip, so its true rate is
 * independently known to be 4000 - its decode locks 80-90%, which it
 * could not do at the wrong period.
 */
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/ratefit.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

static int failures = 0;

static void check(bool cond, const char *what)
{
    printf("%s: %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond)
        failures++;
}

int main()
{
    std::string info;
    std::vector<double> audio = wav_read_22050("jmh-offair-12k.wav", &info);
    std::vector<uint8_t> video = fm_decode(audio, nullptr);
    printf("fixture: %s\n  -> %zu video samples (%.0f lines)\n",
           info.c_str(), video.size(), video.size() / 4000.0);

    /* Too short to fold: must refuse outright, not guess. A wrong rate
     * applied confidently is worse than no correction at all. */
    std::vector<uint8_t> stub(video.begin(),
                              video.begin() + 20 * 4000);
    RateFit sf = fit_line_period(stub);
    check(!sf.ok, "20 lines -> ok=false (refuses to guess)");
    check(sf.period == 4000.0, "  ...and leaves the period nominal");

    /* The untouched fixture: a real chart determines its own rate, and
     * that rate is the nominal one. */
    RateFit base = fit_line_period(video);
    printf("  unmodified: period %.3f (%+.1f ppm), skew_px %.1f, prom %.2f\n",
           base.period, base.ppm, base.skew_px, base.prom);
    check(base.ok, "a real chart determines its own rate");
    check(std::fabs(base.period - 4000.0) < 0.05,
          "  ...and the untouched fixture measures 4000 +- 0.05");

    /* Round trip over three imposed errors. -120 ppm (3999.52) is what
     * the GYA/KiwiSDR recordings actually showed; +250 checks the other
     * sign; -500 checks a value near the edge of the sweep. */
    const double cases[] = { 3999.52, 4001.00, 3998.00 };
    for (int i = 0; i < 3; i++) {
        const double want = cases[i];
        /* video_retime(v, P) maps P input samples onto 4000 output ones.
         * To IMPOSE a period of `want` on a 4000-period stream, retime by
         * the reciprocal ratio - the inverse of what the fit will undo. */
        std::vector<uint8_t> skewed =
            video_retime(video, 4000.0 * 4000.0 / want);
        RateFit rf = fit_line_period(skewed);
        printf("  imposed %.2f -> measured %.3f (%+.1f ppm), skew_px %.1f\n",
               want, rf.period, rf.ppm, rf.skew_px);

        char msg[160];
        snprintf(msg, sizeof msg,
                 "  measures an imposed %.2f-sample period to +-0.05", want);
        check(rf.ok && std::fabs(rf.period - want) < 0.05, msg);

        std::vector<uint8_t> fixed = video_retime(skewed, rf.period);
        RateFit back = fit_line_period(fixed);
        snprintf(msg, sizeof msg,
                 "  correcting it returns the stream to 4000 (got %.3f)",
                 back.period);
        check(std::fabs(back.period - 4000.0) < 0.05, msg);
    }

    /* Noise has no line structure at any period, so the fold curve is
     * flat and `prom` stays near 1. This is the guard that stops a
     * meaningless measurement being applied - note skew_px alone would
     * NOT catch it, because a flat curve on a short stream can still
     * have a narrow spurious tip. */
    std::vector<uint8_t> noise((size_t)(RATEFIT_MIN_LINES + 20) * 4000);
    unsigned int seed = 12345;
    for (size_t i = 0; i < noise.size(); i++) {
        seed = seed * 1103515245u + 12345u;
        noise[i] = (uint8_t)((seed >> 16) & 0xff);
    }
    RateFit nf = fit_line_period(noise);
    printf("  noise: period %.3f, skew_px %.1f, prom %.2f\n",
           nf.period, nf.skew_px, nf.prom);
    check(!nf.ok, "noise -> ok=false (no line structure to measure)");
    check(nf.prom < RATEFIT_MIN_PROM, "  ...rejected on prominence, as designed");

    printf(failures ? "\nFAILED (%d)\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
