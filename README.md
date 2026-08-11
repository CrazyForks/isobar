# Isobar

<img src="assets/isobar-512.png" alt="Isobar icon" width="120" align="right">

[![Build](https://github.com/skgsara/isobar/actions/workflows/build.yml/badge.svg)](https://github.com/skgsara/isobar/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/skgsara/isobar?include_prereleases)](https://github.com/skgsara/isobar/releases)

**A cross-platform HF weather-fax (WEFAX, emission J3C) decoder.**

Isobar decodes the weather-fax images broadcast over shortwave by agencies
like JMH (Japan), NMG (USA), and BMV/ARC (various) — using only your
computer's sound card and an HF receiver. It is an independent,
from-scratch reimplementation that is interoperable with the
long-standing hobbyist software **KG-FAX v1.1.3** (K.G, 2009): it reads
and writes KG-FAX `.syn` files, and uses the same settings schema (stored
as `isobar.ini`, importing an existing `kgfax.ini` on first run).
Recordings load at **any sample rate** — 12 kHz from an SDR, 48 kHz from a
sound card, whatever your receiver software writes — and in either way a WAV
can spell 16-bit PCM, including the `WAVE_FORMAT_EXTENSIBLE` header macOS
`afconvert` produces when converting from `.m4a`.

> **Not affiliated with, endorsed by, or derived from KG-FAX.** Isobar's
> source is original, written from a functional specification produced by
> reverse engineering. See [`NOTICE`](NOTICE) for the full provenance
> statement and [`docs/README.md`](docs/README.md) for the legal footing.

## Download

Prebuilt, **self-contained** binaries for macOS, Windows, and Linux are
attached to each [**Release**](https://github.com/skgsara/isobar/releases)
— no FLTK/RtAudio install needed on the target machine:

- **macOS** — `Isobar-v<version>-macOS-AppleSilicon.dmg` (M1 and later) or
  `Isobar-v<version>-macOS-Intel.dmg`. Each holds an `Isobar.app` bundle with
  the FLTK/RtAudio dylibs embedded in `Contents/Libraries/`; ad-hoc signed, so
  right-click → Open the first time to clear Gatekeeper. Needs macOS 15 or
  newer, since the bundled libraries are built against it.
- **Windows** — `Isobar-v<version>-windows.zip` (a portable folder; FLTK +
  RtAudio are statically linked into the `.exe`, so there are no DLLs to
  install). x64; on Windows-on-ARM machines it runs under the system's own
  x64 emulation.
- **Linux** — `Isobar-v<version>-linux-x86_64.AppImage` or
  `Isobar-v<version>-linux-aarch64.AppImage` (a single file — `chmod +x` and
  run; the FLTK/RtAudio `.so`s are bundled inside). The `aarch64` one is for ARM
  boards such as the Raspberry Pi. Both are built on Ubuntu 22.04, so they
  need glibc 2.35 or newer (Debian 12 / Raspberry Pi OS bookworm and up).

To build from source instead, see [Build](#build).

## What it does

- **Decodes WEFAX** (WMO-No. 386 Part III §5): 1500/2300 Hz FM sub-carrier
  about 1900 Hz, 60 or 120 rpm. IOC-selection (300 Hz) / stop
  (450 Hz) tone detection arms and disarms capture automatically.
  IOC 576 broadcasts only, and at the original program's 1500-px line
  rather than the standard's 1810 — see
  [Where this stops, and why](#where-this-stops-and-why).
- **Stations without per-line sync** (WMO phasing only, e.g. VMW Wiluna):
  locks from the phasing preamble, then holds the phase through the chart
  at the line rate measured off that preamble, following stream dropouts
  from the picture's own content.
- **Live reception** from any sound input, or **offline decode** from a WAV
  recording. Spectrum scope + waterfall shown live during reception.
- **Image tools**: zoom/pan, vertical rotate toggle, XY flip, 4 color
  palettes, BMP/PNG export (color when a palette is applied, grayscale
  otherwise), print. BMP and grayscale PNG files can also be loaded back
  as images.
- **Auto-save** on stop tone or at max scan width: `.syn`, `.bmp`, or
  grayscale `.png` (the small archival format — every chunk's CRC is
  checked when it is read back, so a file damaged in storage is reported
  rather than quietly decoded as a corrupt chart).
- **KG-FAX interop**: round-trips `.syn` files (including the radix-255
  line-count encoding and palette mode/invert bits in the header), and
  reads the older legacy "Syn Fax" variant (fixed 2000×2280 body).
- **Survives audio dropouts**, which matters if you feed it a networked
  SDR (KiwiSDR and friends) over a long internet path — see below.

### Networked SDRs and audio dropouts

An internet-fed SDR loses audio when the stream stalls, and every lost
chunk shifts the sync position for everything after it. Isobar follows
that shift on the line it happens on, so **the size of a dropout barely
matters**: measured by injecting gaps into a real off-air recording, a
*ten-second* outage costs no damaged lines at all — you lose the ten
seconds of chart it swallowed and nothing more.

What does matter is how *often* they happen, and the limit is the
`LockAfter` setting (Details… dialog, default **5**): acquiring sync needs
that many consecutive undisturbed line periods, i.e. 2.5 seconds of clean
audio. At the default, one dropout every 3 seconds is fine (5% of lines
damaged); one every 2 seconds and it never locks at all.

**If your feed drops out more often than that, lower `LockAfter` to 1–3.**
It costs nothing on clean audio — 5, 3 and 2 all decode a good recording
identically — and it turns the one-dropout-every-2-seconds case from
unusable into 5% of lines damaged. The longer chain exists to stop false
locks on *noisy* HF; a networked SDR feed is the opposite problem, clean
but gappy, so it can afford a short one.

## Build

Isobar is plain C++17 built with **CMake**. The only third-party
dependencies are [FLTK](https://www.fltk.org/) (GUI),
[RtAudio](https://caml.music.mcgill.ca/~gary/rtaudio/) (live audio), and
zlib (PNG compression in the core — present by default on macOS, and
pulled in by the Linux/Windows packages below).

```sh
cmake -B build -S .
cmake --build build           # builds isobar-decode, isobar-gui, tests
ctest --test-dir build        # runs the 23 headless tests
```

Install the dependencies first:

| OS | Command |
|---|---|
| macOS | `brew install fltk rtaudio cmake` |
| Debian/Ubuntu | `sudo apt install libfltk1.3-dev librtaudio-dev zlib1g-dev cmake` |
| Windows | vcpkg: `fltk` and `rtaudio`, with `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` (MSVC) |

### Run

```sh
# Decode a WAV recording to an image:
./build/isobar-decode recording.wav out.pgm

# If the recording starts mid-chart and comes out sheared, measure the
# receiver's clock error from the picture and correct it:
./build/isobar-decode --fit-rate recording.wav out.pgm

# Or open the GUI (then pick your radio's audio input):
./build/isobar-gui        # Linux / Windows
open ./build/Isobar.app   # macOS (the GUI builds as a .app bundle)
```

On macOS the GUI builds as a proper `Isobar.app` bundle (with icon and the
required microphone-usage permission prompt); the first launch needs a
right-click → Open to clear Gatekeeper (the app is ad-hoc signed, not
notarized).

## Project layout

```
core/   C++17 DSP core: FM demod, sync, .syn, FFT, tone detect,
        live scan, resample, BMP/PNG, palette. Only external dependency:
        zlib (PNG compression). See core/README.md.
cli/    isobar-decode (the decoder CLI) + the headless test suite.
gui/    FLTK GUI: main window, scope, dialogs, RtAudio capture.
docs/   Functional specification (derived from reverse engineering) + plans.
        Read docs/README.md first, then docs/01-program-analysis.md.
        docs/06-release-process.md covers releasing (read before tagging).
cmake/  Templates filled in at configure time: the version header
        (isobar_version.h.in; one source of truth) and the Windows
        resource script (isobar.rc.in; exe icon + version metadata).
assets/ App icons (.icns / .ico / PNGs) — generated from jmh-portrait.svg
        via tools/make-icons.sh.
macos/  Info.plist template for the .app bundle.
tools/  make-icons.sh (icon regeneration) + extract_dfm.py (dev/research).
```

## Status

**v1.8.0 — the final release.** The project set out to reimplement KG-FAX
as a portable, modern program, and that is done. Development stops here;
see [Where this stops, and why](#where-this-stops-and-why).

Working software: all core receive features are
implemented and verified on real JMH recordings; see [`ROADMAP.md`](ROADMAP.md)
for the milestone map (M0–M5 done; M6 = validation & release, largely
done — two validation items still open).
Stations that send no per-line sync — WMO phasing only, VMW Wiluna being
the verified case — decode as well, on a phase held from the preamble
rather than tracked (`DEVIATIONS.md` #19).
Reception is not limited to the original's 2280-line buffer: long charts
(e.g. XSG's ~2755-line broadcasts) are captured in full up to 4560 lines,
while `.syn` saves stay KG-FAX-compatible (2280 lines max).
Continuous builds run on macOS, Linux, and Windows — Intel and ARM alike —
via GitHub Actions (`.github/workflows/`); tags `v*.*.*` produce five
self-contained native release packages (two macOS `.dmg`s, a Windows `.zip`,
and x86_64 + aarch64 `.AppImage`s) attached to a
[GitHub Release](https://github.com/skgsara/isobar/releases).

## Where this stops, and why

Isobar is finished, and it is finished on purpose rather than abandoned.
The goal was a faithful, portable KG-FAX — one that reads and writes its
files, keeps its settings schema, and runs on machines the 2009 Windows
binary never could. That works. Along the way it also grew past the
original in the places real recordings demanded: stations that send no
per-line sync pulse, networked-SDR audio dropouts, and receiver clock
error (`DEVIATIONS.md` #19, #20).

The next thing on the list would have broken that promise, so it was not
done. **The line width is 1500 px, which is not the IOC 576 the standard
asks for.** IOC 576 wants π·576 ≈ 1810 px per line; IOC 288 wants
π·288 ≈ 905. 1500 is neither — it is the original program's number,
inherited along with everything else, and the specification derived from
the binary says as much: *"IOC 288/576 implied by the 1500-px line width,
no explicit IOC constant."*

This is measurable, not theoretical. The aspect-ratio circle on the JMH
test chart decodes **46 × 55 px, aspect 0.836**, against the **0.829**
that 1500/1810 predicts. **Every image this program produces is about 17%
too narrow.**

Three things are worth knowing before anyone treats that as a bug to fix
here:

- It is an **output-raster fault only**. Sync and timing run on the
  4000-sample line, so it has nothing to do with the clock-error and
  dropout work. Identical code measures 0.0 ppm of clock error on a
  non-KiwiSDR recording and −80 to −120 ppm on KiwiSDR ones, and the test
  chart locks 94.1% of its lines at 1500 px.
- Correcting it properly means **breaking `.syn` interoperability**,
  which is the compatibility promise the whole project rests on: the
  format is 1500 bytes per line. A faithful port and a standards-correct
  decoder are different products, and this is the boundary between them.
- Widening the raster **alone would recover no detail**. The
  demodulator's video lowpass is 1200 Hz, which limits the stream to
  roughly 1200 usable pixels per line; 1500 already oversamples that.
  True IOC 576 resolution needs the lowpass opened up too, and whether
  an HF signal carries that detail is an open question nobody here has
  measured.

There is a second, deeper inheritance worth naming. KG-FAX's sync design
assumes the station sends a **black sync pulse on every line**. JMH does —
a solid 59-px strip, which locks 83–91%. GYA, NMC and VMW send nothing at
all in the picture; a column-mean scan finds no dark column. Isobar
handles those anyway, by holding a phase measured off the phasing
preamble, but the assumption is still in the architecture. A decoder
built to the WMO standard from the start would not make it.

**If you want a standards-correct WEFAX decoder, start a fresh project and
take what is useful from here** — `docs/07-starting-fresh.md` says what is
worth reusing and what should be left behind. Reimplementing KG-FAX and
implementing WMO-No. 386 are two different jobs, and this repository has
finished the first one.

## License

Copyright © 2026 Sara Sakuragawa. Licensed under the **GPLv3+** — see
[`LICENSE`](LICENSE). Links to FLTK (LGPLv2 + static-linking exception) and
RtAudio (MIT); see [`NOTICE`](NOTICE) for third-party attribution.
