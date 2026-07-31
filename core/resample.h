/* resample.h - streaming anti-alias resampler to 22050 Hz.
 *
 * Live audio sources rarely run at 22050 Hz natively (USB sound
 * devices typically offer 44100/48000). This mirrors the two stages
 * used elsewhere in the core: windowed-sinc lowpass, then linear-
 * interpolation pick-off (same as FmDecoder's 22050 -> 8000 stage;
 * the 300-2300 Hz signal sits far below the output Nyquist).
 */
#ifndef ISOBAR_RESAMPLE_H
#define ISOBAR_RESAMPLE_H

#include "filters.h"

#include <vector>

struct Resampler {
    Fir    lpf;
    double step_in;    /* input samples per output sample (fs_in/fs_out) */
    double next_out;   /* position of next output rel. to current input  */
    double prev;       /* previous filtered input sample */

    /* fs_out cutoff is placed just below the lower of the two
     * Nyquist frequencies (~10 kHz for 22050 involved). */
    void init(double fs_in, double fs_out);

    /* Feed one input sample; appends 0 or more output samples. */
    void feed(double x, std::vector<double> &out);
};

#endif
