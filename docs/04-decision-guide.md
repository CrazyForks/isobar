# Decision Guide — Option A vs Option B

> **Decision (2026-07-29): Option A was chosen, but with FLTK (not Qt) as the
> GUI toolkit and RtAudio for live audio — see the banner at the top of
> `02-option-a-cross-platform.md`. The Qt framing below is retained as the
> historical comparison; substitute FLTK for Qt when reading. The DSP-core
> milestone (M0) is done, and M0–M5 are complete.**

Both options share the same first milestone (M0: framework-agnostic C++ DSP core
+ CLI validation harness). **Work can start on M0 before this decision is made.**

## Side-by-side

| | **A: Qt + RtAudio** | **B: Swift + AppKit** |
|---|---|---|
| Platforms | macOS, Linux, *BSD (one codebase) | macOS only |
| Total effort | ~6–10 weeks | ~4–7 weeks |
| Cost of adding Linux later | ~0 (already done) | Full GUI rewrite (re-pay ~40%) |
| Replica fidelity (2009 VCL look) | High — QWidget ≈ VCL | Lower — AppKit fights the retro look |
| Pixel-blit / waterfall drawing | Natural (`QImage.scanLine`, `QRubberBand`) | Workable (`NSBitmapImageRep`, custom views) |
| Audio | RtAudio (extra abstraction layer) | CoreAudio/AVAudioEngine (direct, clean) |
| Native macOS feel | Poor (but original has none) | Good (only matters if deviating from replica) |
| Packaging burden | ×3 platforms, Qt bundling, LGPL compliance | One `.app`, sign + notarize |
| Language | C++ throughout | Swift GUI + C++ core (bridging) |

## Decision checklist

Ask the user, in order:

1. **Will this ever need to run on Linux or BSD?**
   - Yes / probably → **Option A**. This is the dominant factor.
   - Definitely macOS-only → continue.
2. **Is strict window-for-window fidelity the goal, or is "inspired-by with a
   native Mac feel" acceptable?**
   - Strict replica → **Option A** (Qt reproduces the VCL look more faithfully).
   - Native Mac feel welcome → **Option B** becomes attractive.
3. **Which stack does the user want to live in for weeks?**
   - Comfortable in C++/Qt → A. Prefers Swift → B (accepting the caveats).

If answers conflict (e.g., "macOS-only but strict replica"), state the
trade-off explicitly and let the user pick; default recommendation: **A**.

## Either way — non-negotiable first steps

1. **Obtain the original `kgfax.exe`** (freeware; ham archives) — needed as a
   golden reference under Wine/CrossOver. (Not for extracting FIR taps: those
   are re-derived as standard windowed-sinc designs from the passband specs —
   see `01-program-analysis.md` §9. Reading the decompile for behavioral facts
   is established practice; transliterating code out of it is not.)
2. **Collect test signals**: record JMH off-air (3620.6 / 7793.1 / 13986.6 kHz
   USB, 120 rpm, per readme.txt), keep as 22050 Hz 16-bit mono WAV.
3. **Build M0**: implement the decoder fresh from `01-program-analysis.md`
   §3 (NOT by transcribing the decompiled functions — see the legal policy);
   CLI WAV → `.syn`; diff against golden output. Define the equivalence
   tolerance (recommend: byte-identical `.syn`, else ±1 LSB/pixel with
   identical geometry).
4. **Write DEVIATIONS.md** at project start, seeding the known deliberate
   deviations:
   - DSP runs on a decoder thread, not the GUI thread (original's
     `TThread::Synchronize` design is the cause of the readme's
     window-animation warning — not replicated).
   - `SNDVOL32.EXE` buttons → platform mixer/settings equivalent.
   - Any intentional UI modernizations (Option B especially).

## Definition of "bug-for-bug" for this project

- **Replicate exactly**: DSP output for identical PCM input; `.syn` format;
  ini schema (including `LReSycn`/`RReSycn` typos); sync/slant/palette/zoom/
  rotate semantics; the 10 forms' layout and control behavior.
- **Deliberately do not replicate**: GUI-thread DSP serialization;
  Windows-mixer shell-outs; anything recorded in DEVIATIONS.md.
