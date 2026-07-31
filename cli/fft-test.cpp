/* fft-test - quick correctness check for core/fft.cpp.
 * Not part of `all`; build with `make fft-test`, run ./fft-test.
 *
 * A sine at frequency f sampled at 22050 Hz must peak at bin
 * round(f*4096/22050): 1000 Hz -> bin 186, 2300 Hz -> bin 427.
 */
#include "../core/fft.h"

#include <cmath>
#include <cstdio>

static const double PI = 3.14159265358979323846;
static const double FS = 22050.0;

static double in[FFT_N];
static double db[FFT_BINS];

/* returns the bin of the largest magnitude */
static int peak_bin()
{
    int best = 0;
    for (int k = 1; k < FFT_BINS; k++)
        if (db[k] > db[best])
            best = k;
    return best;
}

static int check(double freq, int expect_bin)
{
    for (int i = 0; i < FFT_N; i++)
        in[i] = 20000.0 * sin(2.0 * PI * freq * i / FS);
    fft_4096(in, db);
    int pk = peak_bin();
    /* 20000 -> 86.0 dBFS; allow a few dB scalloping loss off bin center */
    int ok = (pk == expect_bin) && (db[pk] > 80.0) &&
             (db[pk] > db[expect_bin + 40] + 40.0);
    printf("%7.1f Hz: peak bin %d (expect %d), %.1f dB  %s\n",
           freq, pk, expect_bin, db[pk], ok ? "PASS" : "FAIL");
    return ok;
}

int main()
{
    int ok = check(1000.0, 186);
    ok &= check(2300.0, 427);
    printf(ok ? "fft-test: PASS\n" : "fft-test: FAIL\n");
    return ok ? 0 : 1;
}
