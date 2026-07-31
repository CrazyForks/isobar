/* resample-test - verifies the streaming resampler.
 *
 * A 1900 Hz sine (mid-subcarrier) at 44100 and 48000 Hz is resampled
 * to 22050 Hz. Checks: output length within 1%, dominant frequency
 * (zero crossings) within 1%, RMS within 5% of the input's.
 * Exit 0 on pass, 1 on fail.
 */
#include "../core/resample.h"

#include <cmath>
#include <cstdio>

/* Local PI — see core/tonedetect.cpp (M_PI is missing under MSVC). */
static const double PI = 3.14159265358979323846;

static int run_case(double fs_in)
{
    const double fs_out = 22050.0, freq = 1900.0, amp = 10000.0;
    const int n_in = (int)(fs_in * 4.0);   /* 4 s */

    Resampler rs;
    rs.init(fs_in, fs_out);
    std::vector<double> out;
    double sumsq_in = 0;
    for (int i = 0; i < n_in; i++) {
        double x = amp * sin(2.0 * PI * freq * i / fs_in);
        sumsq_in += x * x;
        rs.feed(x, out);
    }

    /* length check */
    double expect = n_in * fs_out / fs_in;
    if (out.size() < expect * 0.99 || out.size() > expect * 1.01) {
        fprintf(stderr, "FAIL %.0f Hz: length %zu, expected ~%.0f\n",
                fs_in, out.size(), expect);
        return 1;
    }

    /* skip the filter warm-up (first 0.5 s), then zero crossings */
    size_t skip = (size_t)(fs_out * 0.5);
    int crossings = 0;
    for (size_t i = skip + 1; i < out.size(); i++)
        if ((out[i - 1] < 0) != (out[i] < 0))
            crossings++;
    double secs = (out.size() - skip) / fs_out;
    double f_meas = crossings / 2.0 / secs;

    double sumsq = 0;
    for (size_t i = skip; i < out.size(); i++)
        sumsq += out[i] * out[i];
    double rms = sqrt(sumsq / (out.size() - skip));
    double rms_in = sqrt(sumsq_in / n_in);

    fprintf(stderr, "%5.0f -> 22050 Hz: %zu samples, f=%.1f Hz, "
            "rms %.0f (in %.0f)\n", fs_in, out.size(), f_meas, rms, rms_in);

    if (fabs(f_meas - freq) > freq * 0.01) {
        fprintf(stderr, "FAIL %.0f Hz: frequency off\n", fs_in);
        return 1;
    }
    if (fabs(rms - rms_in) > rms_in * 0.05) {
        fprintf(stderr, "FAIL %.0f Hz: rms off\n", fs_in);
        return 1;
    }
    return 0;
}

int main()
{
    if (run_case(44100.0) || run_case(48000.0))
        return 1;
    printf("OK: resampler verified\n");
    return 0;
}
