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

## Status

Phase: **working software, pre-release**. Milestone status lives in
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
workflow is complete; M6 (packaging & cross-platform CI) done — v1.1.1
released, CI green on macOS/Linux/Windows, Intel and ARM. **First real reception
verified 2026-07-30** (phone speaker → MacBook mic, JMH sample). Build system
is **CMake** (`cmake -B build && cmake --build build && ctest --test-dir build`);
test fixture is the committed 30 s excerpt `jmh-sample-short.wav` (44.1 kHz
stereo Int16; a full 9.6 min recording exists locally but is gitignored —
97 MB, too large for git; tests auto-detect and use whichever is present).

## How the next contributor should use this

1. Read `01-program-analysis.md` to understand what must be replicated.
2. The A-vs-B decision is **made**: Option A with FLTK (see Status above).
   Skip `03-…` except as historical reference.
3. Implementation follows `02-…` (FLTK variant); the DSP-core milestone is
   done. Deliberate deviations from the original go in `DEVIATIONS.md`.
4. `ROADMAP.md` has the milestone-level map of done vs pending. (Maintainers
   also keep a private `SESSION-LOG.md` for session-to-session handoff; it is
   not published.)

## What gets published vs. stays private

This project's legal footing depends on keeping the original program's
copyrighted expression out of the public repo. The rule:

**Published (in the public repo):** all original source (`core/`, `cli/`,
`gui/`), the functional specification in `docs/` (decompile line-number
references and addresses stripped — `01-program-analysis.md` is now spec, not
a code dump), `CMakeLists.txt` + `cmake/` (build system), `assets/` (icons),
`macos/` (Info.plist template), `tools/` (icon-gen + DFM-extract scripts),
the test fixture `jmh-sample-short.wav`, `LICENSE`, `NOTICE`, `ROADMAP.md`,
and this index.

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
