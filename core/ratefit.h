/* ratefit.h - measure a reception's true line period from the picture.
 *
 * WHY THIS EXISTS (DEVIATIONS.md #20). A WEFAX line is nominally 4000
 * video samples (0.5 s at 8000 S/s). When transmitter and receiver clocks
 * disagree by e ppm the real period is 4000*(1+e) and the chart comes out
 * sheared. Networked SDRs make this routine: a KiwiSDR without GPS or a
 * TCXO is typically 80-120 ppm off, differently per unit, and 120 ppm is
 * 244 px of skew across a 1352-line chart.
 *
 * The decoder normally gets the rate for free: WMO phasing anchors feed
 * a least-squares fit (live.cpp's inv_drift, DEVIATIONS #19) and the line
 * is held at the measured period. That covers a reception that starts at
 * the top of a chart, which is the usual case.
 *
 * It does NOT cover a reception that starts mid-chart - tuning in late,
 * or a recording cut past the preamble. Stations that send no per-line
 * sync pulse in the picture (GYA, NMC, VMW all do not; JMH is the
 * exception with its 59-px black strip) then have no clock reference at
 * all and the chart free-runs at the nominal 4000.
 *
 * This measures the period from the picture itself instead. Fold the
 * video stream modulo a trial period and average: at the right period
 * every vertical feature - sync strip, chart border, meridians, the
 * frame edge - stacks up and the folded profile is sharp; at the wrong
 * one they smear across it and it flattens. Maximise the variance of the
 * folded profile over the trial period.
 *
 * It is a measurement, not a constant. Nothing here is tuned to a
 * particular receiver, and there is deliberately no fallback "typical"
 * value: an oscillator error that cannot be measured is not corrected.
 */
#ifndef ISOBAR_RATEFIT_H
#define ISOBAR_RATEFIT_H

#include <vector>
#include <cstdint>

struct RateFit {
    double period;  /* samples per line at 8000 S/s (nominal 4000)      */
    double ppm;     /* (period - 4000) / 4000 * 1e6                     */
    double halfw;   /* the largest offset from the peak, in samples, at
                       which the fold score still holds 90% of its
                       maximum. NOT comparable between recordings: a
                       peak narrows as lines are added, so this is ~0.01
                       on an 11-minute chart and ~0.37 on a 60-second
                       one at the same quality. Use `skew_px`.          */
    double skew_px; /* halfw expressed as what it costs: the pixels of
                       skew the uncertainty allows across the WHOLE
                       reception. Length-independent, and the quantity
                       actually worth judging.                          */
    double prom;    /* peak height over the sweep's mean score. Says the
                       picture has line structure at all: a chart runs
                       well above 1, noise sits at ~1. Separate from
                       skew_px, which only says the peak is narrow -
                       a flat curve can have a narrow spurious tip.     */
    bool   ok;      /* both tests passed: worth applying                */
};

/* Sweep limits. +-1500 ppm covers every KiwiSDR seen so far (worst
 * measured: -637 ppm, and that one was dropouts rather than clock) with
 * room to spare, while staying far short of half a line, where the fold
 * would start locking onto the wrong feature. */
const double RATEFIT_MIN_PERIOD = 3994.0;
const double RATEFIT_MAX_PERIOD = 4006.0;

/* Acceptance thresholds, both calibrated against real recordings rather
 * than chosen - see cli/ratefit-test.cpp and the S40 session log.
 *
 * RATEFIT_MAX_SKEW_PX: the fit is worth applying when its uncertainty
 * costs less than this many pixels of skew across the whole reception.
 * Measured over thirteen recordings the usable fits land at 4.6-58.4 px
 * and the two useless ones - a noise-limited chart and one broken by a
 * network dropout - at 232.6 and 305.7. 100 sits in the middle of that
 * gap rather than at the edge of the good range. It is a discriminator,
 * not a quality bar: 100 px of residual is still a large win against
 * the 200-500 px errors this exists to remove.
 *
 * RATEFIT_MIN_PROM: peak-to-mean ratio of the sweep. Real charts sit
 * far above this; a stream with no line structure sits at ~1. */
const double RATEFIT_MAX_SKEW_PX = 100.0;
const double RATEFIT_MIN_PROM = 1.5;

/* Fewer lines than this and the fold has too little to stack up. */
const int RATEFIT_MIN_LINES = 40;

/* Measure the line period of a whole video stream (8000 S/s bytes,
 * 0 = black). Returns ok = false when the picture does not determine it
 * - too short, or a flat fold curve - in which case `period` is still
 * the best guess but should not be applied. */
RateFit fit_line_period(const std::vector<uint8_t> &video);

/* Resample a video stream so that `period` input samples become exactly
 * 4000 output samples, i.e. undo the clock error the fit measured.
 * Linear interpolation: the ratio is within ~1500 ppm of 1, so there is
 * no meaningful aliasing to filter (the same argument as FmDecoder's
 * 22050 -> 8000 pick-off). Feed the result to scan_lines as-is. */
std::vector<uint8_t> video_retime(const std::vector<uint8_t> &video,
                                  double period);

#endif
