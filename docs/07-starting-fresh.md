# Starting fresh: what to reuse from Isobar, and what to leave behind

Isobar is a faithful reimplementation of KG-FAX. If what you want instead
is a decoder built to WMO-No. 386 Part III from the start, do not extend
this repository — start a new one and take the parts of this that are
about *radio*, not the parts that are about *KG-FAX*.

This document exists so the next project inherits the knowledge without
inheriting the constraints. It was written at the end of Session 40, when
the decision to stop was made; the reasoning behind that decision is in
`README.md` ("Where this stops, and why").

---

## The one-paragraph version

Take the DSP. Leave the geometry. The FM demodulator, the resampler, the
WAV reader, the image I/O and the clock-error measurement are all about
receiving a radio signal and are worth lifting more or less as they
stand. The 1500-px line, the 4000-sample line as a universal constant,
the `.syn` format and the assumption that every station sends a per-line
sync pulse are all KG-FAX's, and every one of them will cost you if you
carry it across.

---

## Worth taking

**`core/filters.*`** — windowed-sinc FIR design (lowpass, bandpass,
Hilbert) plus streaming convolution. Textbook, self-contained, no
KG-FAX in it at all.

**`core/decoder.*`** — the FM demodulation chain: bandpass the
1500–2300 Hz subcarrier, quadrature demodulate through a Hilbert
transformer, unwrap the phase difference, scale to 0–255. This is the
part that turns radio into video and it is independent of every
geometry decision. **But see the 1200 Hz note under "Decide for
yourself" below** — the output lowpass is a resolution ceiling, and it
is ours, not the original's.

**`core/resample.*` and `core/wavfile.*`** — arbitrary sample rate in,
22050 Hz out, including the `WAVE_FORMAT_EXTENSIBLE` header macOS
`afconvert` writes for anything converted from `.m4a`. Unglamorous and
well tested; you would only rewrite this for the pleasure of it.

**`core/ratefit.*`** — measures the receiver's clock error from the
picture by folding the video at trial line periods. Written last and
written knowing what the problem actually was; it needs no adaptation
beyond changing which period it is folding *to*. Read the header
comment before reusing: the two acceptance thresholds are calibrated
against real recordings, and the calibration table is in
`DEVIATIONS.md` #20.

**`core/pngfile.*`, `core/bmpfile.*`, `core/palette.*`, `core/render.*`**
— image output, palettes, box-filtered scaling. Nothing station-specific.
`pngfile` checks every chunk CRC on read, which is worth keeping.

**Two ideas, more than the code that implements them:**

- *Measure the line rate off the phasing preamble and hold it.* The WMO
  phasing signal is a rate reference, not just a phase reference. Fitting
  a period to its pulses and holding the line at that rate is what makes
  a networked SDR's 80–120 ppm clock error disappear. See `DEVIATIONS.md`
  #19 and #20.
- *Follow dropouts using the picture's own content.* When a stream loses
  audio and the station sends no per-line sync, nothing else will ever
  notice. Ink-gated line-to-line correlation with one line of lookahead
  does (`sync_content_step` in `core/syncscan.h`). The ink gate is the
  part that matters — without it, false steps in a masthead band tore a
  chart's logo in half.

**The test fixtures and what they prove.** `docs/README.md` lists each
committed recording and the specific failure it exists to catch. That
list is worth more than any single piece of code here: it is eleven real
receptions with known, documented pathologies — a mid-chart start, three
network dropouts, an inverted-phasing station, a 2755-line chart, a
satellite image, a station with no per-line sync. Re-point them at a new
decoder and you inherit years of "this broke once" for free.

---

## Leave behind

**The 1500-px line.** IOC 576 is π·576 ≈ 1810 px; IOC 288 is
π·288 ≈ 905. 1500 is KG-FAX's number and matches neither. Measured on
the JMH test chart, the aspect circle comes out 46 × 55 px (0.836)
against 1500/1810 = 0.829 predicted — every Isobar image is ~17% too
narrow. **Derive your raster from the IOC, and carry the IOC as an
explicit constant**, which this codebase never does.

**4000 samples per line as a universal constant.** It is 0.5 s at
8000 S/s, i.e. 120 rpm, and it is threaded through the sync code, the
buffers and the file format. 60 rpm is handled by *halving the video
rate* so the 4000 can stay (`video_halve_rate`), which works but tells
you how deeply the constant is wired in. A fresh design should hold line
duration and sample rate separately and compute the rest.

**The `.syn` format**, unless you specifically want KG-FAX interop. It
caps at 2280 lines, stores 1500 bytes per line, and encodes its line
count in radix-255. Every one of those is a constraint on the rest of
the design. Isobar carries an internal buffer that grows to 4560 lines
precisely because real charts (XSG's ~2755) do not fit the format.

**The assumption that stations send a per-line sync pulse.** This is the
deepest inheritance and the least visible. KG-FAX's sync detector looks
for a black run of 100–400 samples every line, and JMH — the station its
author could hear — sends exactly that, a solid 59-px strip that locks
83–91%. GYA, NMC and VMW send **nothing**: a column-mean scan of their
decoded charts finds no dark column anywhere. Isobar copes by holding a
phase measured off the preamble, but that is a workaround layered on an
assumption. Design for "there may be no per-line reference at all" from
the start and the whole sync layer comes out simpler.

**The original's tunable names and defaults** (`Sync2Thre`, `LReSycn`,
`RReSycn`, `SyncWidth`…). They are preserved here for settings-file
compatibility and they are not a good vocabulary. Note one finding worth
carrying over as *behaviour* though: the lock-acquisition chain length
(`LockAfter`, default 5) is what decides whether a gappy internet feed
locks at all. On clean audio 5, 3 and 2 decode identically; on a feed
dropping every 2 seconds, 5 never locks and 3 gives 78 locked lines with
5.3% damaged. Long chains defend against *noisy HF*; networked SDRs are
the opposite problem — clean but gappy — and want short ones. Consider
adapting it automatically rather than exposing a number.

---

## Decide for yourself (open questions, not settled here)

**The 1200 Hz video lowpass.** `FmDecoder`'s output filter cuts at
1200 Hz, which limits the stream to roughly 1200 independent pixels per
line — so Isobar's 1500-px raster already oversamples, and widening the
raster alone would fix the aspect ratio while recovering no detail. That
cutoff is **ours**, a re-derivation: the original's filter taps sit in
its binary as an unread data blob, so nobody here knows what KG-FAX
actually used. Before choosing a raster width, measure whether HF WEFAX
carries detail above 1200 Hz at all — look at the spectrum of the
demodulated video on a strong recording. If it does not, IOC 576's 1810
px is a geometry requirement rather than a resolution one, and that
changes what "correct" costs.

**IOC 288.** Selected by a 675 Hz start tone where IOC 576 uses 300 Hz.
KG-FAX detects only 300 Hz and is therefore IOC-576-only; Isobar
inherited that. A standards decoder should detect both and set its
geometry from the tone. No recording of a 675 Hz start exists in this
repository, so it has never been tested here.

**Aspect handling on output.** Even at a correct raster width, decide
explicitly whether a saved image is square-pixel or whether the aspect
is carried as metadata. Isobar writes raw pixels and lets the viewer
guess, which is part of why the 17% error went unnoticed for forty
sessions.

---

## Provenance, if you copy code across

Everything in `core/` was written from `docs/01-program-analysis.md`, a
functional specification produced by reverse engineering, and **not** by
translating the decompiled original. That policy is stated in
`docs/README.md` and it is binding on anything derived from this
repository. If you lift code from here, you inherit that history: it is
clean, and keeping it clean means continuing to work from the
specification rather than from the decompile. The decompiled artifacts
(`kgfax.exe.c`, `kgfax.exe.asm`, `readme.txt`) are excluded by
`.gitignore` and were never committed.

Isobar is GPLv3+. Code taken from it carries that license.
