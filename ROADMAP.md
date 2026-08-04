# Roadmap — milestones and status

Big-picture tracker of milestones and status. This is the public map of what's
done vs pending; a private `SESSION-LOG.md` (not published) carries the
session-to-session journal.

Legend: ✅ done · 🔶 partial · ⬜ not started

## M0 — DSP core + CLI ✅
WAV in → decoded image out, no GUI. Verified on a real JMH recording
(1157 lines, ~81% shape-locked, rest corrected/coasted by the fallback —
the line count rose from the original 1002 after the Session 5 scanner
rewrite to the original's fixed 4000-sample grid cadence).
- ✅ WAV reader (44.1k/22.05k, stereo→mono, decimation)
- ✅ FM demod chain (BPF → Hilbert/atan2 → LPF → decimate to 8000 S/s)
- ✅ Sync detect + line assembly (120 rpm), PGM output
- ✅ `.syn` read/write (SynFax2 + legacy magic, byte-exact round-trip;
  line-count field fixed to the original's radix-255 encoding 2026-07-31 —
  interop with original KG-FAX confirmed by header math)
- ✅ 60 rpm mode (video halved to 4000 S/s, scan unchanged; CLI `--60`,
  GUI rpm combo; `ctest -R rpm60-test` — no real 60 rpm recording yet)
- ✅ Sync fallback: min-brightness search + lock/release hysteresis
  (`ctest -R fallback-test`; regenerated reference out.pgm — fallback-region
  lines now land on corrected positions)
- ✅ Original's fallback tracker ported (S25) — boxcar minimum-mean over the
  binarised video, gated on a bright→dark edge, absolute `SyncThre` bound
  (`fb_mean` = 30, the original's own value, which transfers now that the
  formula matches) and the `MaxJump` guard. Our dip-depth search stays as a
  second chance when it declines. Mis-phased picture lines across the three
  fixtures: jmh 74→73, phasing fixture 56→28 (the 28 are all preamble and
  stop tone — zero picture damage), off-air 10→**0** with lock ratio
  78.3%→94.2% and relocks 1→0. See DEVIATIONS #16
- ✅ Phase runaway fixed (S25): re-acquisition after a release could pick a
  sync edge from an earlier line (negative rotation offset) or, anywhere in
  the 4000-sample line, a dark feature in the picture — shifting the image
  sideways by up to 3200 samples for tens of lines. Both paths are now
  bounded to the fallback's neighbourhood around the phase already held,
  which is also what the original does (it never re-searches a whole line
  once locked). `ctest -R phasing-test`; the two older fixtures decode
  byte-identically
- ✅ 300/450 Hz tone detection (resonators on the video stream, threshold
  calibrated on the sample's real start/stop tones; `ctest -R tone-test`;
  drives the Control LED — flashes by too fast to eyeball on file decode,
  built for live audio)

## M1 — Main window skeleton ✅
Exact TForm1 geometry (888×551) extracted from the original binary's form
resources (`tools/extract_dfm.py` → `docs/05-gui-layout.md`). English UI
(DEVIATIONS #5). Open `.syn`, Decode WAV (worker thread), Save `.syn`,
Clear, Quit all functional.
- ✅ Layout matches original coordinates (screenshot-verified)
- ✅ Preview (760×500, the original's Image1): always the column view —
  one column per 3 lines, line start at the bottom, no scrolling

## M2 — Live display ✅
- ✅ Spectrum scope (4096-pt FFT, 1157–2773 Hz, 1500/2300 markers), fed live
- ✅ LEDs from real sync state (Sync det / Sync corr) + Control LED from
  300/450 Hz tone levels (DetTime hysteresis)
- ✅ Waterfall shown together with the spectrum like the original;
  click = pause/resume (screenshot-verified)

## M3 — Settings ✅
- ✅ ini persistence in the original's structure, stored next to the
  executable like the original — `<exe-dir>/isobar.ini` (was `~/kgfax.ini`
  until 2026-07-30, then `<exe-dir>/kgfax.ini` until v1.2.0, when the file
  was split from the original's and the six tuning keys renamed to say what
  they mean; see `DEVIATIONS.md` #6 and #16). An existing `kgfax.ini` is
  imported once, preferences only.
- ✅ Details… dialog (Form4 replica, English)
- ✅ All sync settings live (MaxJump/FallbackDepth/DarkThreshold/
  ReleaseAfter/LockAfter + syn combo → fallback window); ToneBlocks live
  for tone-detection
  hysteresis
- ✅ Save settings on quit (rpm/sync choices, window position; also on
  title-bar close; `--test-quit-save` dev flag)

## M4 — Remaining dialogs & image tools ✅
Layouts all extracted in `docs/05-gui-layout.md`; implemented with English captions:
- ✅ Form5 color processing (4 palette modes, invert-first, `.syn`
  header round-trip; `gui/colordialog.*`; `--test-color` /
  `--test-palette` dev flags, screenshot-verified)
- ✅ Form6 bitmap save options + BMP export (`gui/exportdialog.*`,
  `core/bmpfile.*`; native chooser instead of Form3, BMP-only, no
  clipboard — DEVIATIONS #12; orientation labels deliberately swapped,
  Land. = 90° CCW rotated / Port. = as-is — DEVIATIONS #14;
  `--test-export` + user hand-test verified)
- ✅ Form8 auto-save settings (`gui/autosavedialog.*`; native dir
  chooser instead of listboxes — DEVIATIONS #13; SpeedButton3
  open-on-press-down / OK-arm / Cancel-disarm semantics;
  `--test-autosave-dialog` screenshot-verified). Now also carries the
  **output-format radio** (.syn/.bmp, replacing the original's
  FilterComboBox1 — DEVIATIONS #15) and the size radios + CycleGet are
  **live** (the auto-save BMP + restart-at-max-width features landed
  — see M5)
- ✅ Form10 recording notice (`gui/noticedialog.*`) + Form2 progress
  dialog (`gui/progressdialog.*`): both landed in M5. Form7 is orphaned
  in the original — nothing opens it; the Vertical button is an
  in-place 90° rotate toggle instead (done)
- ✅ Zoom/pan on the preview (1/3 → 1/2 → 1:1, click/drag;
  `--test-zoom`), rotate: Vertical = in-place 90° toggle
  (`--test-rotate90`), XY flip = clean 180 (`--test-rotate180`)
- ✅ Print (`cb_print`, Fl_Printer native panel; raster render of the
  received lines with palette, pre-rotate buffer when the 90° toggle is
  active, stretched to the whole page via pre-scaled `copy()` — the
  macOS printer driver ignores scaled `draw()`; user hand-test verified
  2026-07-31; `--test-print` BMP-verified)

## M5 — Live audio reception ✅ (the big one)
- ✅ Audio capture: **RtAudio** (decision 2026-07-29; Homebrew), 22050 Hz
  direct or device-preferred rate + `core/resample` conversion
- ✅ Audio runs from program start (original behavior); Scan = record
  gate into the image
- ✅ Auto ctl: 300 Hz presses Scan, 450 Hz releases it; Auto save writes
  `DirName/YYYYMMDDHHMM.syn` — full cycle loopback-verified
- ✅ Sideways live preview (one column per 3 lines, 3-line average, line
  start at the bottom = y 499−tap, cyan "Scanning / Line# n" HUD +
  frontier cursor — matches original proportions, user-verified)
- ✅ Scanner follows the original's cadence+phase model: fixed
  4000-sample grid from record start (preamble decoded, sync strip part
  of the image), tracking adjusts only the rotation phase; full-window
  fallback before first lock. Batch/live byte-identity kept
- ✅ Scope: spectrum + waterfall simultaneously (original behavior);
  click = pause/resume
- ✅ Sync toggle (SpeedButton1 sync-track enable: OFF freezes phase,
  LEDs black, state 3; re-ON re-acquires; spec in docs/01 §3.2)
- ✅ Manual sync align (S21): with the Sync button released, clicking the
  live preview just below the sync signal seeds the phase from the click
  (8·(500−y) samples) and resumes tracking there instead of re-acquiring
  — the second half of the readme's 同期処理の停止と手動同期位置指定.
  Spec + departures in docs/01 §3.2; `cli/manual-sync-test.cpp`
- ✅ Input-device chooser (Form9 replica, WaveDev persisted; `@menu` icon
  button; Volume button deactivated — no macOS equivalent); chooser
  switches the live stream; startup falls back to the default input if
  the saved device fails
- ✅ `cli/playwav` dev tool: plays a WAV to any output device (loopback
  testing via BlackHole, no radio needed)
- ✅ **First real reception verified** (2026-07-30, phone speaker →
  MacBook mic, JMH sample): lines from the very start, correct
  proportions, solid lock
- ✅ Buffer clear/rotate while receiving: **CycleGet** restart at max
  width — at 2280 lines, with auto-save armed on the `.syn` filter, the
  buffer is saved then cleared mid-reception so capture continues
  (matching the original's `sub_40C858` CycleGet path, docs/01 §4).
  Auto-save output is a **format choice** (.syn default / .bmp) via the
  new Form8 radio group (replaces the original's FilterComboBox1;
  JPEG dropped — DEVIATIONS #15); the size radios now drive the BMP
  render (3×3 / 2×2 / 1:1, verified at all three sizes via
  `--test-autosave`). `.syn` clears the buffer, `.bmp` does not (the
  asymmetry is in the trace, preserved). **Form10** recording-notice
  popup (`gui/noticedialog.*`, shown during input-device switch) and
  **Form2** progress dialog (`gui/progressdialog.*`, shown during the
  save/print/auto-save renders) added; the Form2 bar is near-instant
  on modern hardware (it flashes). Dev flags `--test-notice`,
  `--test-progress`, `--test-autosave [file] [fmt] [size]`

## M6 — Validation & release 🔶
- 🔶 Golden-reference comparison against the original — **kept open**.
  The original `kgfax.exe` is a 32-bit Windows program (2009) and cannot
  run on the dev machine (Apple Silicon: Rosetta 2 is 64-bit-only, and
  Wine/CrossOver on these Macs runs 64-bit Windows apps only — the hardware
  can't execute 32-bit code at all). Deferred until access to a machine
  that can run it (Intel Mac on Mojave or earlier, or a Windows/Linux PC
  with Wine). NOT a blank gap, though — substantial cross-checking has
  already happened by other means: decoder built fresh from a reverse-
  engineered spec; output hand-tested against reference photos of the
  original several times (orientation, the sideways chart, scope layout);
  and in Session 7 our `.syn` files were loaded INTO the original KG-FAX
  to catch the line-count encoding bug — a direct interop test against
  the real program.
- ✅ Project name + license + NOTICE done (Session 10). Name **Isobar**;
  license **GPLv3+**; `LICENSE` + `NOTICE` written. Binaries renamed
  `kgfax-*` → `isobar-*`; window title, CLI strings, include guards all
  updated (`kgfax.ini` filename KEPT = interop — **reversed in v1.2.0**, see
  the S23 notes and `DEVIATIONS.md` #6). `docs/01` cleaned: private
  banner removed, all decompile line-number refs + VA addresses + `asm:`
  refs stripped (Hex-Rays symbol names `sub_XXXX` kept — they're IDA
  output, not K.G identifiers, and the second-AI audit confirmed they're
  fine). Docs fixes from the legal audit: PORTING.md no longer names
  decompile artifacts, docs/README contents table adds 05-gui-layout,
  DEVIATIONS #11 gap explained (period-nudging removed in S5), docs/03
  Form6→Form9 corrected, "clean-room" overclaim softened to
  "reimplementation from a spec". AGENTS.md/START-HERE.md/SESSION-LOG.md
  gitignored (medical info — user decision).
- ✅ Contact original author (K.G) — **attempted 2026-08-01 (S20);
  undeliverable, and no contact channel for him exists.** Sent within hours
  of the first public release, before any promotion. The bilingual letter
  used the agreed 互換実装 framing (NOT 復刻, NOT 個人研究 — those misframe
  GitHub publication), plain factual tone, no "please permit me" (creates a
  false authorization narrative). Rejected by his own mail server; an
  identical rejection came back from a second, unrelated sending provider
  the same day, so the failure is at the receiving end and the mailbox is
  gone. QRZ.com and the 総務省 station-licence registry return nothing for
  JJ0OBZ. Both bounces are documented privately outside the repo (medical/
  personal folder, like AGENTS.md). The DMCA-risk rationale is satisfied as
  far as it can be: he was written to at his own published address, twice,
  by independent routes. **This item is closed — do not reopen it.**
- ✅ Compiler warnings on the non-Mac toolchains — **all fixed in v1.3.0
  (S25) and verified on CI: 0 warnings on all five runners**, re-confirmed
  2026-08-02 on run 30769561399. Two of them were more than cosmetic: the
  gcc `-Wformat-truncation` was a genuine truncation in `set_title()` (now
  built on `std::string`), and `-Wmaybe-uninitialized` was real —
  `syn_read()` never set `FaxImage::lines_corrected`. What they were: **MSVC** 6× C4996 (`fopen` in `core/synfile.cpp` ×2,
  `core/bmpfile.cpp`, `gui/main.cpp`, `cli/kgfax-decode.cpp`; `localtime` in
  `gui/main.cpp`) plus 1× C4244 `double`→`float` in `gui/progressdialog.cpp`;
  **gcc** `-Wformat-truncation` in `gui/main.cpp` (a `%s` that can write 159
  bytes into 151 — the one worth actually reading, not just silencing) and
  `-Wmaybe-uninitialized` on `rot` in `core/syncscan.h`. Fix pattern for the
  The C4996s became `std::ofstream`/`ifstream`, as `core/settings.cpp`
  already did, and `localtime` got a portable `local_time()` wrapper
  (`localtime_s` on MSVC, `localtime_r` elsewhere) — which also removed a
  real thread-safety wart. Verified by CI branch runs, not locally, because
  the dev machine cannot see any of them.
- 🔶 Adapt the lock chain to a gappy source (networked SDRs) — **open,
  measured 2026-08-02, no code written.** Lock acquisition needs a chain of
  `LockAfter` (RReSycn, default **5**) consecutive valid line periods, i.e.
  **2.5 s of undisturbed audio**, and an audio dropout breaks the chain.
  That, not anything in the sync tracker, is what sets the decoder's
  tolerance for a KiwiSDR or other internet-fed source. Measured by
  injecting dropouts into `jmh-offair-12k.wav` (120 lines, 113 locked,
  0 damaged clean):

  | one 120 ms drop every | locked | damaged lines |
  |---|---|---|
  | 5 s | 99 | 0 |
  | 3 s | 91 | 6 (5.2%) |
  | **2 s** | **0** | **96 (85%)** |
  | 1 s | 0 | 102 (95%) |

  Dropout *size* is almost irrelevant — a **10-second outage costs 0
  damaged lines** (you lose 20 lines of chart and nothing else), because
  `sync_step_lock` follows any phase step within a line. Only the *rate*
  matters, and the cliff between 3 s and 2 s is exactly the 5-chain:
  lowering `LockAfter` to 3 turns the every-2 s case from 0 locked / 85%
  damaged into 78 locked / 5.3% damaged, and `LockAfter` 1 rescues the
  every-1 s case (49 locked / 8.4%). **Lowering it costs nothing on clean
  audio** — 5, 3 and 2 all give 113 locked / 0 damaged.

  So today the answer is manual: lower `LockAfter` in the Details dialog
  (already a 1..200 spinner) when listening to a networked SDR. The
  hysteresis exists to stop false locks on *noisy HF*; a KiwiSDR feed is
  the opposite regime — clean but gappy — and can afford a short chain.
  **The future work**: notice that the chain keeps breaking and shorten it
  automatically, rather than making the user know this. Care needed so it
  cannot loosen itself on a genuinely noisy HF signal, which is where the
  hysteresis earns its keep — the signal to key off is "periods keep
  starting and then being interrupted", not "no lock".

- 🔶 Packaging & cross-platform CI — **5-phase arc all ✅ done + GREEN on
  GitHub (Sessions 11–15)**. Repo is live at github.com/skgsara/isobar;
  `build.yml` passes on all five runners — macos-latest / macos-15-intel /
  ubuntu-latest / ubuntu-22.04-arm / windows-latest (S22 widened the matrix
  from three; Windows ARM is deliberately absent, see the v1.1.1 notes):
  0. ✅ **Pre-publish repo audit** (S13) — `.gitignore` hardened and
     dry-run-verified (no private/derivative files would be tracked); test
     fixture trimmed to a committed 30 s excerpt (`jmh-sample-short.wav`,
     5.3 MB, 100% lock) with tests made sample-agnostic (pass on excerpt
     AND full WAV). **S23 added a second fixture**, `jmh-offair-12k.wav`
     (1.4 MB, 60 s of a 12 kHz off-air JMH reception): it is the only
     sample that is not 22050/44100 Hz, so it is the only one exercising
     `core/resample.cpp` in the WAV reader, and the only one weak enough
     to drive the fallback sync correction (96 locked / 24 corrected /
     1 relock). Both are float-heavy paths that CI now checks on x86_64
     and aarch64 alike (`offair-test`). **S25 added a third fixture**,
     `jmh-phasing-16k.wav` (2.9 MB, 90 s of a 16 kHz recording of two
     back-to-back HIMAWARI IR charts): the only sample containing a full
     transmission preamble — ~33 s of all-dark phasing lines between two
     pictures — which is what exposed the phase-runaway bug fixed in S25
     (`phasing-test`). **S26 added a fourth**, `jmh-slew-12k.wav` (1.4 MB,
     a second 60 s cut of the same 12 kHz off-air recording, 50..110 s):
     the stretch where that recording's sync position genuinely steps
     −162 samples, which is what a networked SDR's clock-slip correction
     leaves behind (`slew-test`). **S29 added a fifth**,
     `jmh-himawari-12k.wav` (1.4 MB, 60 s at 300..360 s of a KiwiSDR
     HIMAWARI IR reception, resampled to 12 kHz): the only fixture whose
     video is a photo rather than a chart, so it is the only one where the
     darkest thing on a line is usually cloud instead of the sync pulse.
     That is what broke the tracker twice — S26's lock-onto-cloud and
     S28's per-line phase wobble — and neither had a test until now.
     Built against each of those commits, `satellite-test` reports 58 /
     77 / 120 of 120 lines locked, so it fails loudly on either
     regression. S29 also added `reacq-test`, which needs no fixture of its
     own: it splices the demodulated video (dropping N samples mid-decode,
     as a networked SDR's dropped buffer does) and requires the tracker to
     re-phase. That closed the last open sync item — re-acquisition escapes
     a bad phase via `sync_step_lock` on the line the step happens, and
     `widen_after`, which S27/S28 suspected of never firing, turns out to be
     reachable but redundant on every fixture. Stale `make`/`Makefile` refs
     fixed across all docs; root `README.md` written (GitHub landing page).
  1. ✅ **CMake migration** (S11) — `CMakeLists.txt` replaces the Makefile;
     `isobar_core` lib + `isobar-decode`/`isobar-gui` + 7 ctest tests.
     FLTK via `fltk-config --use-images`, RtAudio via `pkg-config` (no
     more hardcoded `/opt/homebrew` path). Dev flow: `cmake -B build -S .`
     then `cmake --build build` then `ctest --test-dir build`.
  2. ✅ **Cross-platform source fixes** (S12) — `_WIN32` branch added to
     `exe_dir()` in `core/settings.cpp` (`GetModuleFileNameW`); scanned
     and found NO other posix-isms (Windows file APIs accept `/`).
  3. ✅ **Icon + macOS .app** (S12) — icon set generated into `assets/`
     (`.icns`/`.ico`/PNGs from `jmh-portrait.svg` via
     `tools/make-icons.sh`); `macos/Info.plist.in` with bundle id +
     **`NSMicrophoneUsageDescription`**; CMake builds `Isobar.app`,
     ad-hoc signed. `resource_dir()` helper finds resources in flat or
     `.app` layouts; window icon wired at runtime; Details dialog shows
     the portrait + replaces stale "port in progress" with © Sara
     Sakuragawa / GPL v3+ / real version. **User-verified: Dock icon +
     Details dialog both show the portrait.**
  4. ✅ **GitHub Actions matrix** (S14, green S15) — `.github/workflows/build.yml`:
     `macos-latest`/`ubuntu-latest`/`windows-latest` matrix (install deps
     → cmake build → ctest → upload raw binaries). **GREEN on all 3 OSes**
     after the S15 debugging pass below.
  5. ✅ **CPack release formats** (S14) — install rules + CPack config in
     `CMakeLists.txt` (macOS `.dmg` DragNDrop, Windows `.zip`, Linux
     tarball); `.github/workflows/release.yml` on `v*.*.*` tags: matrix
     build → ctest gate → cpack → renamed native artifacts attached to a
     GitHub Release (auto-generated notes, `-` in tag = prerelease).
     AppImage deliberately deferred (needs linuxdeploy; tarball is the
     simple Linux story). **Not yet exercised end-to-end on a real tag.**
     *(Self-containment landed in S17 — see below; Linux deliverable is now
     an AppImage, not a tarball.)*
  **S14 must-fixes (made the code cross-platform):**
  `RtAudio::MACOSX_CORE` hardcoded → default ctor in `gui/audio.cpp`
  (×2) + `cli/playwav.cpp`; `M_PI` → local `PI` constant in
  `core/tonedetect.cpp` + `cli/tone-test.cpp` + `cli/resample-test.cpp`
  (MSVC); CMake MSVC flag handling (guard `add_compile_options` with
  `if(NOT MSVC)`); added config-mode `find_package(FLTK/RtAudio CONFIG)`
  for vcpkg/Windows + fixed the nonexistent `PkgConfig::fltk_images`
  target. Docs: 2 legal-policy blockers (decompile-artifact names in
  published docs) + ~10 stale-fact fixes (Form4 captions, BSD→Windows,
  `kgfax-port/`→`isobar/` tree, core/README omissions, dead SESSION-LOG
  pointers). `.gitignore` now also catches chat-export dumps.
  **S15 must-fixes (got CI green — bugs no Mac-only dev box could catch):**
  RtAudio v5/v6 API split → `gui/rtaudio_compat.h` shim (Debian apt=v5
  throws; Homebrew/vcpkg=v6 returns errors); FLTK 1.3/1.4 `Fl_Printer`
  `start_job(pagecount)` vs `begin_job()` → `FL_API_VERSION` guard in
  `gui/main.cpp`; GNU ld single-pass link ordering → feed fltk-config
  ldflags via `target_link_libraries` not link_options (Linux); vcpkg
  FLTK 1.3 exports bare `fltk`/`fltk_images` targets with EMPTY
  `FLTK_LIBRARIES` AND ships `fltk-config` (Unix script) that silently
  links nothing under MSVC → skip fltk-config on WIN32, probe target
  names, `FATAL_ERROR` on empty, add Win32 system libs (comctl32/
  comdlg32/ole32 for Fl_Printer/Fl_Native_File_Chooser).
  **Repo pushed** (S15): github.com/skgsara/isobar, full commit
  history kept (9 commits incl. diag — user decision, shows real work).
  **Released v1.0.0** (S16): `release.yml` on the `v1.0.0` tag builds per-OS
  packages (macOS `.dmg`, Windows `.zip`, Linux tarball) via CPack and
  attaches them to a GitHub Release. CI green on macOS/Linux/Windows.
  Proven via a `v1.0.0-rc1` dry-run first (deleted after). **These packages
  were dynamically linked** (needed FLTK/RtAudio pre-installed) — superseded
  by v1.0.1 below.
  **Released v1.0.1 — self-contained packages** (S17): the per-OS packages
  now run WITHOUT FLTK/RtAudio installed on the target machine, each via the
  mechanism native to that platform's package manager:
  - **Windows** — vcpkg `x64-windows-static-md` triplet statically links
    FLTK + RtAudio (and png/jpeg/zlib) into the `.exe`. No DLLs to ship.
    Dynamic CRT (/MD) kept (matches CMake default; present on every Windows
    machine). Proven by the 7-test ctest gate passing on a static-linked
    binary on the windows-latest runner.
  - **macOS** — `dylibbundler` embeds the 5 Homebrew FLTK/RtAudio dylibs
    into `Isobar.app/Contents/Libraries/`, rewrites install names to
    `@executable_path/../Libraries`, then re-codesigns ad-hoc. Verified
    locally AND on the real CI-built binary: 644K raw → 2.8M bundled, zero
    Homebrew paths remain (incl. transitive), app launches. A release.yml
    step gates on `otool -L | grep homebrew` failing the job on any leak.
  - **Linux** — AppImage via `linuxdeploy` REPLACES the tarball. linuxdeploy
    runs `ldd` and bundles every non-system `.so` into a single
    `Isobar-<ver>-linux.AppImage` (chmod +x and run; renamed per-architecture
    in v1.1.1). Static ELF is
    impossible on Linux (glibc not static-linkable; apt `librtaudio-dev`
    ships no static lib), so this is the standard self-contained Linux
    deliverable. New `assets/isobar.desktop` drives linuxdeploy.
  `build.yml` switched to the same static-md Windows triplet so the PR gate
  tests the same linkage the release produces. No source changes; the ctest
  gate is unchanged. Proven via a `v1.1.0-rc1` dry-run first (4/4 jobs
  green, all 3 packages downloaded and verified self-contained), then
  tagged `v1.0.1` on main. Squash-merged as PR #1.
  **Released v1.0.2** (S19): patch release — two bug fixes since v1.0.1
  (the S18 stale `sakuragawasara/isobar` URL fix + the S19 Form4
  sync-preset combo restored to the original's 3 named presets Strict/
  Normal/Gentle). No build/packaging changes; same self-contained
  per-OS packages.
  **Released v1.1.0** (S21): first **minor** bump — one new feature plus one
  bug fix. Feature: manual sync align (click the live preview to place the
  sync position by hand; see the M4 list above). Fix: Print was stretching a
  partial reception `2280/N` too tall, because the port renders only the
  received lines but still scaled them over the whole page — it now scales
  by the original's fixed 2280-line ruler, so the geometry matches KG-FAX
  and the unreceived part is left blank instead of black. Both verified by
  the user on real audio and a real printer. Also: Details/Info panel
  reflowed (name+version on one line, FLTK/RtAudio credited) and the dead
  RtAudio homepage URL replaced in `NOTICE`/`README.md`. No build/packaging
  changes; same self-contained per-OS packages.
  **Released v1.1.1 — packaging (S22):** five packages instead of three.
  The macOS `.app` was Apple-Silicon-only (dylibs from `/opt/homebrew`;
  Intel-Mac `/usr/local` paths differ) — there is now a second `.dmg` built
  on an Intel runner, shipped separately rather than as one universal binary
  (user decision: each is tested on the CPU it targets, and dropping Intel
  later is one matrix entry to delete). Linux gains an `aarch64` AppImage
  for Raspberry-Pi-class boards. Windows stays x64 — Windows-on-ARM runs it
  under the OS's own emulation. Release runners are now **pinned**, because
  the runner's OS sets the package's minimum supported OS: `macos-latest`
  had become macOS 26, so v1.1.0 shipped an `.app` with `minos 26.0` that
  required Tahoe; `macos-15` brings that back to macOS 15, and `ubuntu-22.04`
  (glibc 2.35) reaches Raspberry Pi OS bookworm. Also fixed: the Windows
  `.exe`s carried no icon and no version metadata (nothing compiled a
  resource script), and `isobar-gui` was linked for the console subsystem,
  so a console window opened behind the app. One source change came with it:
  `<cstddef>` added to `core/syncscan.cpp` + `core/decoder.cpp`, which the
  new gcc-11 runners rejected (`size_t` used unqualified, and older
  libstdc++ does not leak it through `<vector>`). No decoder behaviour
  changed — hence a patch bump.
  **Released v1.5.1 — audit fixes + the FallbackDepth correction (S30–S31):**
  a full-project audit (five read-only passes over docs, core, gui, cli/tests,
  repo hygiene) found the DSP sound but three genuine GUI bugs, all fixed:
  the auto-save dialog's size radios were never initialised from the saved
  setting (reopening it silently reset the image size to 760x500); the
  recording-notice and progress popups centered on screen corner (0,0)
  instead of the main window; and live audio was never paused for a WAV
  decode, so the RtAudio thread and the decode worker raced on the shared
  tone-level/scope globals (now guarded by an atomic flag). The audit's
  robustness leftovers landed too: RtAudio calls wrapped so broken audio
  can no longer kill the app, the Auto ctl button pops back up when audio
  fails to start, and LiveScan's counters are atomic. The one user-visible
  behaviour change: the Details dialog's "Sync detect" setting
  (`FallbackDepth` ini key) now drives `fb_mean` — the original's SyncThre
  quantity — instead of our dip-depth second chance, so the original's
  default **30** is right again and is the new default (DEVIATIONS #16
  follow-up; `fb_thresh` stays hard-coded at 10). **An `isobar.ini` written
  by v1.2.0–v1.5.0 carries `FallbackDepth=10`, which this build reads as a
  stricter `fb_mean=10` — set Sync detect back to 30 ("Normal") in the
  Details dialog once.** Also fixed: both scrollbars permanently visible
  around the image view (Fl_Scroll children are window-absolute; the
  FaxView now sits at the scroll's inner-box origin). ~30 stale
  comments/docs swept, dead code removed, and `kgfax-decode` renamed
  `isobar-decode` — the last `kgfax-` filename. A splice-fill repair for
  dropout tears was prototyped and deliberately dropped: it could
  phantom-repair an intact line, and a false repair is worse than a tear.
  Test count unchanged at 14; no new fixtures.
  **Released v1.5.0 — read the WAVs `afconvert` writes, and test the
  satellite path at last (S29):** two gaps left open by v1.4.0, plus one
  worry that turned out not to be a gap at all.
  `core/wavfile.cpp` now accepts **`WAVE_FORMAT_EXTENSIBLE`** (fmt tag
  `0xFFFE`): the real format tag lives in the first two bytes of the
  SubFormat GUID, whose other 14 bytes are the fixed
  `KSDATAFORMAT_SUBTYPE` suffix and are verified, so anything that is not
  really PCM is still rejected. macOS `afconvert -f WAVE` writes that
  header whenever the source carries a channel layout — every `.m4a`, so
  every phone or KiwiSDR recording converted that way — and the samples
  inside were ordinary 16-bit PCM all along. Only the label differed, and
  the reader was checking the label. Rejection messages now name the
  format found and, for IEEE float, the `afconvert` line to re-convert
  with.
  Two new tests, and a fifth fixture, `jmh-himawari-12k.wav` — 60 s of a
  KiwiSDR HIMAWARI IR reception at 12 kHz, the first fixture whose video
  is a **photo** rather than a chart. `satellite-test` guards the case
  that had broken the tracker twice with nothing testing it (S26's
  lock-onto-cloud, S28's per-line phase wobble); built against those two
  commits it reports 58 and 77 of 120 lines locked against 120 now, so it
  fails loudly on either. Because a locked line can still sit a few pixels
  out — which is exactly what S28's bug did — it also cross-correlates
  adjacent line pairs and counts those needing a shift over 3 px: 31
  before `sync_anchor`, 1 after.
  `reacq-test` closes the last open sync item, and closes it by
  measurement rather than by code: S27 and S28 both suspected
  re-acquisition could not escape a wrong phase because `widen_after` was
  never reached. It **is** reached (49 lines of the full himawari
  reception, 27 of `jmh-phasing-16k`) — but it is not what rescues the
  tracker either. `sync_step_lock` is, on the line the step happens.
  Splicing the demodulated video, as a networked SDR's dropped buffer
  does: recovery from a 200/800/2000-sample step is immediate with or
  without a dead stretch before it; with `sync_step_lock` removed 35 of 74
  lines stay mis-phased, with both routes removed all 74. `widen_after` is
  kept — it is the only way back when no unambiguous pulse can be found at
  all, which no fixture covers — and `core/syncscan.cpp` now carries those
  numbers so this is not re-opened from the same wrong premise.
  ctest 12 → 14. CI housekeeping with it: the workflow actions were far
  enough behind to be running on a deprecated Node (checkout v4 → v7,
  upload-artifact v4 → v7, download-artifact v4 → v8, cache v4 → v6), and
  the macOS dependency step now installs only what is missing, so
  "already installed and up-to-date" stops burying real warnings. No
  compiler warnings on any of the five runners, before or after.
  The branch dry run before tagging earned its keep on this release, and
  what it found is now written down in **`docs/06-release-process.md`**
  (new): the procedure, the documentation checklist that goes with a bump,
  and the failures that have actually happened. Its first rule is to push
  `main` after every session — all four problems this release hit were in
  code that had never reached a runner, because `main` had sat unpushed
  since S27. Two were fatal (both Linux jobs failed to compile on an
  unqualified `size_t` in `syncscan.h`; every packaging job failed on a
  slash in the branch name) and two were the MSVC warnings above.

  **Released v1.4.0 — follow real steps in the sync position (S26):** the
  user reported that whenever the **Sync corr** LED lit, the black sync
  strip stopped being one solid bar — half stayed at one end of the line
  and half appeared at the other, and those lines were unreadable. The
  strip is rotated to index 0, the seam where a line wraps, so a phase
  error of N samples does not shift a line: it **splits** the strip,
  putting N samples of black at the far end. 22 of the 23 line-to-line
  phase jumps over 20 px on the off-air recording were on a Sync corr line.
  The cause was not noise. Reading the true sync position straight off the
  video shows it stepping 2131 → 1969 in **one line** and holding the new
  position for the rest of the reception — a real discontinuity, 162
  samples, further than either the shape check (±`MaxJump` = 20) or the
  fallback (±`syn`/2 = 80) can reach; `jmh sample.wav` does the same at
  line 930 where a new transmission starts. Every previous behaviour
  crawled toward it over a dozen lines, splitting the strip on each.
  `sync_step_lock()` follows it on the line it starts on: the darkest
  `syn` window in the **whole** line, taken only when the pulse is
  unambiguous (nearest rival ≥ 300 samples away and ≥ `DarkThreshold`/2
  brighter, sitting in a 100..400-sample dark run) and the **next** line
  agrees — one line of lookahead. `sync_slew()` rate-limits everything
  that is not a confirmed step. Both are inline in `core/syncscan.h`,
  shared by `scan_lines()` and `LiveScan` so the two cannot drift.
  Rejecting far results outright, which is what the original does, was
  measured and is unusable: it raised mis-phased picture lines on the
  off-air recording from 36 to 211, because it rejects the real steps too.
  Results — line pairs moving the strip more than 5 px: `jmh sample.wav`
  129 → 101, off-air **56 → 6**, the new fixture **16 → 2**; mis-phased
  picture lines off-air **36 → 9**, lines locked 1747 → 1803.
  Two costs, both deliberate: half a second of live latency (a completed
  line is held until the next arrives) and `LiveScan::finish()` plus its
  thread-safe `request_finish()`, without which a reception's image ended
  one line short. CI caught a regression the dev machine structurally
  could not — `manual-sync-test` silently changes fixture depending on
  whether the gitignored full sample is present, and on the committed
  excerpt the whole-line search walked off the position the user had
  pointed at, which is the one thing that feature exists to override.
  ctest 11 → 12 (`slew-test`), fourth fixture `jmh-slew-12k.wav`.

  **Released v1.3.0 — sync robustness + the original's fallback tracker
  (S25):** a third user-supplied recording (16 kHz, two back-to-back
  HIMAWARI IR charts, the first with a full transmission preamble) decoded
  with its top quarter torn — ~283 lines shifted sideways by up to 1200 of
  1500 px, on a signal whose true sync position never moves. Two defects,
  both ours. Re-acquisition was bounds-checked only against the top of the
  line window, so it could lock onto an edge from an *earlier* line and
  drive the rotation offset negative (at the first line, indexing outside
  the video buffer); and it searched a whole 4000-sample line, so after the
  ~33 s all-dark phasing preamble released the lock it re-locked onto a
  dark feature in the picture. Re-acquisition is now confined to the
  neighbourhood of the phase already held, widening again only after
  3 × `ReleaseAfter` lines with no lock — because a *genuine* change of
  transmission must still be followed, as `jmh sample.wav` does at line 930.
  **The original's fallback tracker is ported** (`fallback_edge`): boxcar
  minimum-mean over the binarised video, gated on a bright→dark edge, with
  the absolute `SyncThre` bound and the `MaxJump` guard. Our older
  dip-depth search stays as a second chance where it declines — measured,
  the pair beats either alone. `SyncThre` is finally adopted **at the
  original's own value of 30**, because the ported formula computes the
  same quantity; `Sync2Thre` stays ours (DEVIATIONS #16).
  Getting there required correcting `docs/01` §3.2(7)(8) first: the S23
  reading that the original's video is inverted relative to ours was
  **wrong**, and its "sync detector" is really a *phasing-preamble*
  detector — read literally with no inversion, 64 of the new fixture's 66
  phasing lines validate, and 0 validate inverted at any threshold. Also
  found in the decompile and previously undocumented: the fallback is a
  two-boxcar edge test (constants 8 and 128, no ini key) with a jump guard.
  Mis-phased picture lines: jmh sample 74→73, the new recording 196→28 (all
  preamble and stop tone — zero picture damage), off-air 9→**0** with lock
  ratio 78.3%→94.2%. Third fixture `jmh-phasing-16k.wav` + `phasing-test`;
  ctest 10 → 11. All pre-existing MSVC/gcc warnings fixed — **0 compiler
  warnings on all five CI runners**, two of them real bugs (a truncating
  window title, and `FaxImage`'s counters left uninitialised by the rotate
  button). CI: caching vcpkg's binary packages took the Windows job from
  13m41s to 1m29s (it was rebuilding FLTK from source every run).
  **Released v1.2.0 — settings audit + any sample rate (S23):** the user
  supplied the `kgfax.ini` the original writes with untouched settings.
  Comparing it against the port found eight of twelve defaults wrong and,
  more importantly, **three misread settings**, all guesses recorded in
  `docs/01` when the real values were unknown: `SyncWidth` is a
  sync-position jump limit in samples, not a pulse width (the "= 10·n ms"
  note was a misreading of the spinner's step); `DetTime` is a count of
  100 ms blocks, not milliseconds, so Auto ctl waited 100 ms where the
  original waits 2 s; and `LReSycn`/`RReSycn` were swapped (release vs
  lock). The real defaults were confirmed against the `ReadInteger`
  literals in the binary's ini loader.
  Two of them, `Sync2Thre`/`SyncThre`, were deliberately **not** adopted:
  the original's sync detector requires exactly one dark run per line,
  which a clean test-chart signal satisfies (55/60 lines) but a real
  off-air chart never does (0 of 1851, ~30 runs/line from ink and noise) —
  the original leans on its fallback tracker for real charts. Its two
  values in our differently-shaped formulas cost 1550 of 1851 lines. The
  full traced algorithm is now in `docs/01` §3.2(7)(8) as a spec for a
  future port; the difference is `DEVIATIONS.md` #16.
  New: the WAV reader accepts **any sample rate ≥ 6000 Hz** (SDR
  recordings at 8k/12k/48k previously could not be opened at all), reusing
  the existing `core/resample.h`. Settings moved to **`isobar.ini`** with
  the six tuning keys renamed to say what they mean — same file meant two
  keys silently differing between the programs, the worst kind of
  incompatibility. An existing `kgfax.ini` is imported once, preferences
  only. ctest 8 → 9 (`wavrate-test`).
  **Resolved in v1.0.1:** the dynamic-linking limitation that affected all
  3 platforms in v1.0.0.
  **Contacting K.G. — CLOSED 2026-08-01 (S20): attempted twice, undeliverable.**
  Mail to his published address (from the original readme.txt) was rejected by
  his own mail server, and an identical rejection came back from a second,
  unrelated sending provider the same day — so the failure is at the receiving
  end, not a sender block, and the mailbox is gone. His QRZ listing and
  amateur-station licence no longer exist either. All known channels are
  exhausted; both bounces are documented privately outside the repo. Good-faith
  notification is satisfied and this item needs no revisiting; publicity is
  unblocked. **Still pending:** notarize macOS .app if going broad
  (currently ad-hoc = right-click→Open).
  Decisions locked (S11): CMake; native per-OS release formats;
  unsigned/ad-hoc macOS .app for now (Gatekeeper bypass = right-click
  → Open; upgrade to notarized later if/when publishing publicly).
