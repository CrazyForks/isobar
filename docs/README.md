# Isobar — Documentation Index

Isobar is a cross-platform HF weather-fax (WEFAX, emission J3C) decoder for
macOS / Linux / Windows. It is an independent reimplementation, written from a
functional specification produced by reverse engineering, that reproduces the
behavior of — and is fully interoperable with — KG-FAX v1.1.3 (K.G, 2009),
including its `.syn` file format, settings schema, and UI workflow. It is not
affiliated with or endorsed by the KG-FAX author. See `../NOTICE` for the full
provenance and interoperability statement; `../LICENSE` (GPLv3+) for licensing.

This directory holds the functional specification derived from that analysis
and the implementation plans.

## Contents

| File | Purpose |
|---|---|
| `01-program-analysis.md` | Functional specification of the original program: architecture, DSP chain, image pipeline, GUI, config, file formats. Read this first. |
| `02-option-a-cross-platform.md` | Implementation plan: FLTK + RtAudio, single codebase for macOS / Linux / Windows (the chosen path). |
| `03-option-b-macos-native.md` | Alternative plan: Swift + AppKit, macOS only (historical; not chosen). |
| `04-decision-guide.md` | Side-by-side comparison of A vs B and the shared first milestone (DSP core). |
| `05-gui-layout.md` | Exact GUI layout of all 10 forms (captions, coordinates, sizes), extracted from the original binary's form resources. The authoritative layout reference. |
| `06-release-process.md` | How a version gets released, what only CI can catch, and the mistakes that have actually happened. Read before tagging. |

## Status

Phase: **working software — v1.6.0 released**. Milestone status lives in
`../ROADMAP.md`; day-to-day state in `../SESSION-LOG.md` (private — not
published; see "What gets published" below).

**Decision (2026-07-29): Option A, with FLTK as the GUI toolkit** (replaces the
Qt + RtAudio assumption in `02-…`; see the banner at the top of that file).
**Audio backend (2026-07-29): RtAudio** via Homebrew for live input.
Rationale: cross-platform reach without Qt's weight; FLTK's absolute widget
placement matches the original VCL forms; proven by fldigi in this niche.
GUI decorative styling is explicitly *not* a fidelity goal — only window/widget
positions and behavior (per user, 2026-07-29).

Implementation status (see `../ROADMAP.md` for detail): DSP core + CLI done and
verified on a real JMH recording; FLTK GUI with exact Form1 geometry; `.syn`
read/write fully interop-compatible with the original (radix-255 line count,
palette mode/invert round-trip in the header — fixed 2026-07-31); scope with
spectrum + waterfall shown simultaneously (click = pause); settings ini next
to the executable (like the original) + Details dialog; 60 rpm; sync fallback
+ hysteresis; 300/450 Hz tone detection; live audio reception with auto
start/stop/save; Sync-track toggle; Form5 color processing (4 palettes,
invert-first); Form6/Form3 image export (BMP); Form8 auto-save settings;
zoom/pan (⅓/½/1:1 click + drag), Vertical (in-place 90° rotate toggle), XY
flip (180°), and Print. Print + image-export orientation hand-tested by the
user 2026-07-31 (orientation labels deliberately swapped from the original —
DEVIATIONS #14). The main preview always shows the original's column view
(one column per 3 lines, line start at the bottom) — during reception, after
stop, and on `.syn` load alike. The scanner follows the original's cadence+
phase model: lines emit on a fixed 4000-sample grid from record start, sync
tracking adjusts only the rotation phase. **M0–M5 done** — the receive
workflow is complete; M6 (validation & release) is mostly done — v1.6.0
released, CI green on macOS/Linux/Windows, Intel and ARM; the golden-reference
comparison and the gappy-source `LockAfter` change stay open (`../ROADMAP.md`). **First real reception
verified 2026-07-30** (phone speaker → MacBook mic, JMH sample). Build system
is **CMake** (`cmake -B build && cmake --build build && ctest --test-dir build`);
there are six committed test fixtures, each covering something the others
cannot. `jmh-sample-short.wav` is a 30 s excerpt (44.1 kHz stereo Int16) of a
strong signal — the full 9.6 min recording exists locally but is gitignored at
97 MB, and tests auto-detect whichever is present. `jmh-offair-12k.wav` is 60 s
of a real off-air reception at 12 kHz (added v1.2.0): the first fixture
that is not 22050/44100 Hz — four of the five now exercise the resampler in
the WAV reader — and the only one weak enough to drive the fallback sync
correction — both float-heavy
paths that CI checks on x86_64 and aarch64 alike. `jmh-phasing-16k.wav` is 90 s
at 16 kHz of two back-to-back HIMAWARI IR charts (added v1.3.0): the only
fixture containing a full transmission preamble — ~33 s of all-dark phasing
lines between two pictures — which is the case that exposed the v1.3.0 phase
runaway (`phasing-test`). `jmh-slew-12k.wav` is a second 60 s cut of the same
12 kHz off-air recording, from 50..110 s instead of the opening minute
(added v1.4.0): the only fixture containing a genuine **step** in the sync
position — it moves 2131 → 1969 in one line and holds there, which is what a
networked SDR's clock-slip correction leaves behind — and so the only one
that measures whether the sync strip stays solid across it (`slew-test`).
`jmh-himawari-12k.wav` is 60 s of a KiwiSDR HIMAWARI IR reception at 12 kHz
(added v1.5.0): the only fixture whose video is a **photo** rather than a
chart, so the only one where the darkest thing on a line is usually cloud
instead of the sync pulse. That difference broke the tracker twice before
anything tested it (`satellite-test`), and it is also the fixture
`reacq-test` splices to check the decoder can abandon a phase that has gone
wrong — the hard case, because cloud gives a whole-line search plenty of
dark rivals to the real pulse.
`vmw-phasing-12k.wav` is 62 s at 12 kHz of a VMW (Wiluna) reception (added
2026-08-09): the only fixture from a station that sends **no per-line sync
at all** — standard WMO phasing for 30 s before each chart, then nothing in
the picture. Everything the decoder does there it has to do without a
reference: acquire from the inverted (white-in-black) phasing, hold the
phase at the line rate fitted to those pulses, and follow a stream dropout
off the picture's own content (`invphasing-test`).

## How the next contributor should use this

1. Read `01-program-analysis.md` to understand what must be replicated.
2. The A-vs-B decision is **made**: Option A with FLTK (see Status above).
   Skip `03-…` except as historical reference.
3. Implementation follows `02-…` (FLTK variant); the DSP-core milestone is
   done. Deliberate deviations from the original go in `DEVIATIONS.md`.
4. `ROADMAP.md` has the milestone-level map of done vs pending. (Maintainers
   also keep a private `SESSION-LOG.md` for session-to-session handoff; it is
   not published.)
5. Before cutting a release, read `06-release-process.md`. Its first rule —
   push `main` after every session, release or not — exists because three
   sessions once accumulated locally and reached CI carrying a compile error
   that broke both Linux builds.

## What gets published vs. stays private

This project's legal footing depends on keeping the original program's
copyrighted expression out of the public repo. The rule:

**Published (in the public repo):** all original source (`core/`, `cli/`,
`gui/`), the functional specification in `docs/` (decompile line-number
references and addresses stripped — `01-program-analysis.md` is now spec, not
a code dump), `CMakeLists.txt` + `cmake/` (build system), `assets/` (icons),
`macos/` (Info.plist template), `tools/` (icon-gen + DFM-extract scripts),
the six committed test fixtures (`jmh-sample-short.wav`,
`jmh-offair-12k.wav`, `jmh-slew-12k.wav`, `jmh-phasing-16k.wav`,
`jmh-himawari-12k.wav`, `vmw-phasing-12k.wav` — short excerpts of off-air
receptions, each kept
small enough that every clone and CI checkout can afford it; the full
recordings they come from stay local and gitignored), `README.md`,
`PORTING.md`, `DEVIATIONS.md`, `LICENSE`, `NOTICE`, `ROADMAP.md`, and
this index.

**Private (gitignored, never published):**
- The original `kgfax.exe` binary, its Hex-Rays decompile (`kgfax.exe.c`),
  the disassembly (`kgfax.exe.asm`), and the original `readme.txt` — all
  derivative works of K.G's copyrighted expression.
- `SESSION-LOG.md`, `AGENTS.md`, and `START-HERE.md` — personal working
  notes (the session log contains references to private analysis; the other
  two contain personal information and are not for publication).

`.gitignore` enforces this for the decompiled artifacts.

## Legal / provenance policy (binding for all contributors)

KG-FAX is freeware, **not open source**. The decompiled pseudo-C does not
compile; this project is a reimplementation guided by a functional
specification produced from it. Reimplementing the WEFAX decoding algorithm is
standard practice in amateur radio (fldigi, JWX, etc.). The goal is a lawful
open-source release (with optional donations); the rules below are what keep
it lawful. They apply to every contributor and every PR.

### The principle

Copyright protects **expression, not ideas**. The WEFAX protocol (1500/2300 Hz
FM sub-carrier about 1900 Hz, 60/120 rpm, IOC 288/576, 300 Hz IOC-selection and
450 Hz stop tones) is an international standard — **WMO-No. 386, *Manual on the
Global Telecommunication System*, Vol. I, Part III, §5**, with receiver
conformance specified by **ISO 9876:2015** — free for anyone to implement.
That a published conformance standard exists for *building receivers* to this
spec is itself part of the argument: implementing it is the standard's purpose.
Algorithms, mathematical techniques (Hilbert/atan2 demodulation, resonator
tone detection, sync tracking), and functional constants are unprotectable
ideas/facts; where only a few natural implementations exist, the merger
doctrine applies. K.G owns the original code, binary, readme text, name, and
creative assets.

### Allowed

- Writing a fresh implementation from the WEFAX standard and from the
  functional understanding recorded in `01-program-analysis.md` (which is a
  spec, not source).
- `.syn` file-format compatibility (interoperability) and replicating
  functional GUI behavior/layout concepts.
- Reproducing facts from the readme (station frequencies, rpm, tone lengths).
- Re-deriving filter coefficients from specs (e.g., 51-tap BPF, 1500–2300 Hz @
  22050 Hz — a standard windowed-sinc design) instead of copying data tables.
- This codebase is licensed GPLv3+; donations are legally orthogonal to
  copyright (handle them as ordinary taxable income per your jurisdiction).

### Prohibited

- **Never commit or publish `kgfax.exe.c`, `kgfax.exe.asm`, the original
  `kgfax.exe`, the original `readme.txt`, or any hex/data dump of the original
  binary** — these are derivative works of K.G's copyrighted expression. They
  live only in this private working directory and must be gitignored (or kept
  outside the public repo entirely).
- **No transliteration**: do not copy or line-by-line adapt decompiled
  functions (e.g., `sub_402A18`) into the new codebase. Write new code from the
  spec, with your own structure and naming. Convergent similarity on the
  natural approach (atan2 demod etc.) is expected and fine; mirrored control
  flow is not.
- **Do not use the name "KG-FAX"** or branding implying affiliation. The new
  name is **Isobar**; "compatible with KG-FAX `.syn` files" as nominative
  reference is fine.
- **Do not copy the readme text** or original artwork/icons; write fresh docs.

### Before publishing

1. Confirm `.gitignore` (or repo layout) excludes all decompiled artifacts
   and the personal working notes (see "What gets published vs. stays private"
   above).
2. Keep `01-program-analysis.md` free of decompile line-number references and
   raw addresses. (It keeps Hex-Rays symbol names like `sub_402A18` and field
   offsets as spec anchors — those are IDA output labels, not K.G identifiers,
   and were reviewed as acceptable; just don't add new decompile line/VA refs.)
3. Decide the project's own name and license (decided: **Isobar**, GPLv3+);
   `LICENSE` and `NOTICE` state original work, not affiliated with KG-FAX,
   WEFAX per WMO-No. 386 Part III §5 (and ISO 9876:2015 for receiver
   conformance), `.syn` support for interoperability only.
4. As a courtesy in a small community — and to reduce the small but real risk
   of a DMCA notice — **contact the author (K.G) about the project before
   release.** Not a legal requirement, but strongly recommended, and it may
   resolve gray areas (e.g., blessing, or even coefficients).
   **Done 2026-08-01 — undeliverable; no contact channel exists.** He was
   written to at his own published address within hours of first release and
   before any promotion; the mail was rejected by his mail server, and a
   second attempt from an unrelated provider the same day was rejected
   identically, so the mailbox is gone rather than the sender being blocked.
   His QRZ listing and amateur-station licence no longer exist either. The
   attempts are documented privately outside the repo. Nothing further is
   owed on this point; if he ever resurfaces, respond in good faith then.
5. When publishing, publish from a clean repo history (no commit should ever
   have touched the decompiled artifacts).

(Not legal advice; for a published project taking donations, a short consult
with an IP attorney in your jurisdiction is cheap insurance.)
