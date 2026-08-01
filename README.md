<img src="assets/isobar-512.png" alt="Isobar icon" width="120" align="right">

# Isobar

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
sound card, whatever your receiver software writes.

> **Not affiliated with, endorsed by, or derived from KG-FAX.** Isobar's
> source is original, written from a functional specification produced by
> reverse engineering. See [`NOTICE`](NOTICE) for the full provenance
> statement and [`docs/README.md`](docs/README.md) for the legal footing.

## Download

Prebuilt, **self-contained** binaries for macOS, Windows, and Linux are
attached to each [**Release**](https://github.com/skgsara/isobar/releases)
— no FLTK/RtAudio install needed on the target machine:

- **macOS** — `Isobar-<version>-macOS-AppleSilicon.dmg` (M1 and later) or
  `-macOS-Intel.dmg`. Each holds an `Isobar.app` bundle with the FLTK/RtAudio
  dylibs embedded in `Contents/Libraries/`; ad-hoc signed, so right-click →
  Open the first time to clear Gatekeeper. Needs macOS 15 or newer, since
  the bundled libraries are built against it.
- **Windows** — `Isobar-<version>-windows.zip` (a portable folder; FLTK +
  RtAudio are statically linked into the `.exe`, so there are no DLLs to
  install). x64; on Windows-on-ARM machines it runs under the system's own
  x64 emulation.
- **Linux** — `Isobar-<version>-linux-x86_64.AppImage` or
  `-linux-aarch64.AppImage` (a single file — `chmod +x` and run; the
  FLTK/RtAudio `.so`s are bundled inside). The `aarch64` one is for ARM
  boards such as the Raspberry Pi. Both are built on Ubuntu 22.04, so they
  need glibc 2.35 or newer (Debian 12 / Raspberry Pi OS bookworm and up).

To build from source instead, see [Build](#build).

## What it does

- **Decodes WEFAX** (WMO-No. 386 Part III §5): 1500/2300 Hz FM sub-carrier
  about 1900 Hz, 60 or 120 rpm, IOC 576. IOC-selection (300 Hz) / stop
  (450 Hz) tone detection arms and disarms capture automatically.
- **Live reception** from any sound input, or **offline decode** from a WAV
  recording. Spectrum scope + waterfall shown live during reception.
- **Image tools**: zoom/pan, vertical rotate toggle, XY flip, 4 color
  palettes, BMP export, print.
- **KG-FAX interop**: round-trips `.syn` files (including the radix-255
  line-count encoding and palette mode/invert bits in the header).

## Build

Isobar is plain C++17 built with **CMake**. The only third-party
dependencies are [FLTK](https://www.fltk.org/) (GUI) and
[RtAudio](https://caml.music.mcgill.ca/~gary/rtaudio/) (live audio); the
DSP core is dependency-free.

```sh
cmake -B build -S .
cmake --build build           # builds isobar-decode, isobar-gui, tests
ctest --test-dir build        # runs the 9 headless tests
```

Install the dependencies first:

| OS | Command |
|---|---|
| macOS | `brew install fltk rtaudio cmake` |
| Debian/Ubuntu | `sudo apt install libfltk1.3-dev librtaudio-dev cmake` |
| Windows | vcpkg: `fltk` and `rtaudio`, with `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` (MSVC) |

### Run

```sh
# Decode a WAV recording to an image:
./build/isobar-decode recording.wav out.pgm

# Or open the GUI (then pick your radio's audio input):
./build/isobar-gui
```

On macOS the GUI builds as a proper `Isobar.app` bundle (with icon and the
required microphone-usage permission prompt); the first launch needs a
right-click → Open to clear Gatekeeper (the app is ad-hoc signed, not
notarized).

## Project layout

```
core/   Dependency-free C++17 DSP: FM demod, sync, .syn, FFT, tone detect,
        live scan, resample, BMP, palette. See core/README.md.
cli/    isobar-decode (the decoder CLI) + the headless test suite.
gui/    FLTK GUI: main window, scope, dialogs, RtAudio capture.
docs/   Functional specification (derived from reverse engineering) + plans.
        Read docs/README.md first, then docs/01-program-analysis.md.
cmake/  Templates filled in at configure time: the version header
        (isobar_version.h.in; one source of truth) and the Windows
        resource script (isobar.rc.in; exe icon + version metadata).
assets/ App icons (.icns / .ico / PNGs) — generated from jmh-portrait.svg
        via tools/make-icons.sh.
macos/  Info.plist template for the .app bundle.
tools/  make-icons.sh (icon regeneration) + extract_dfm.py (dev/research).
```

## Status

**v1.2.0 released** — working software. All core receive features are
implemented and verified on real JMH recordings; see [`ROADMAP.md`](ROADMAP.md)
for the milestone map (M0–M5 done; M6 = packaging & cross-platform CI).
Continuous builds run on macOS, Linux, and Windows — Intel and ARM alike —
via GitHub Actions (`.github/workflows/`); tags `v*.*.*` produce five
self-contained native release packages (two macOS `.dmg`s, a Windows `.zip`,
and x86_64 + aarch64 `.AppImage`s) attached to a
[GitHub Release](https://github.com/skgsara/isobar/releases).

## License

Copyright © 2026 Sara Sakuragawa. Licensed under the **GPLv3+** — see
[`LICENSE`](LICENSE). Links to FLTK (LGPLv2 + static-linking exception) and
RtAudio (MIT); see [`NOTICE`](NOTICE) for third-party attribution.
