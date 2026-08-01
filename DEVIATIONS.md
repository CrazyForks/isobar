# DEVIATIONS — deliberate differences from original KG-FAX v1.1.3

Everything here is an **intentional** departure from the original program.
If a difference is not listed here, it is a bug to fix, not a feature.
Seeded 2026-07-29 per `docs/04-decision-guide.md`.

## DSP / architecture

1. **Decoder runs on a worker thread, not the GUI thread.** The original
   serializes DSP through `TThread::Synchronize` onto the GUI thread, which is
   the cause of the window-animation warning in the original readme. We keep
   the DSP math identical but move it off the UI thread.
16. **Sync detection is our own algorithm, so `Sync2Thre` and `SyncThre`
    keep our defaults (96 and 10, not the original's 20 and 30).**
    Traced 2026-08-01 (docs/01 §3.2(7)(8)). The original binarises the
    **raw** video at `Sync2Thre` and requires each line to contain
    *exactly one* dark run: it scans one ON run, one OFF run and at most
    one more ON run, then checks the period is ~4000 and the ON total is
    100..400 samples. Its fallback slides a boxcar over the binarised
    signal and accepts the minimum **mean** if that mean is below
    `SyncThre` — an absolute bound, not a dip depth.
    Ours instead thresholds an 8-sample moving average, collects *all*
    candidate edges and chain-matches them across lines, and validates the
    fallback on dip depth below the local mean.
    **Why we did not switch**: measured against our decoder's video, the
    original's single-dark-run requirement is satisfied by 55 of 60 lines
    of a clean test-chart signal but **0 of 1851** lines of a real off-air
    JMH recording (median ~30 dark runs per line, from chart ink and
    noise). The original copes because it falls through to its fallback on
    nearly every real line; our detector locks directly instead, and gives
    1713/1851 lines locked with 0 coasted where feeding it the original's
    two values gives 122 locked and 1550 coasted.
    Both quantities therefore mean something different here, and the
    original's numbers do not transfer. The other ten ini defaults are
    adopted exactly. Porting the original's fallback tracker is a possible
    future exercise; it is fully specified in docs/01 §3.2(8).
7. **Tone detectors run on the 8000 S/s video stream**, not on the
   22050 Hz demod signal before decimation (docs/01 §3.2(5)). The
   300/450 Hz tones pass the decimation unchanged, so detection is
   equivalent; resonator coefficients were re-derived at 8000 with the
   documented formulas, and the threshold was recalibrated (3e9) against
   the real tones in the JMH sample (`core/tonedetect.h`).

## Platform integration

2. **`SNDVOL32.EXE` mixer buttons** (original shells out to the Windows
   mixer) have no macOS per-device equivalent; the Form9 "Volume…"
   button is kept for layout fidelity but **deactivated** for now.
8. **Audio backend is RtAudio** (Homebrew), not Windows waveIn. Streams
   are opened at 22050 Hz when the device allows, otherwise at the
   device-preferred rate with our own resampling. One stream per device
   is kept open for the process lifetime (CoreAudio leaks the
   disconnect listener on close, making reopen fail).

## GUI

3. **Decorative styling is not replicated.** Window frames, widget positions,
   sizes, and behavior match the original form-for-form; colors/fonts/bevels of
   individual widgets follow FLTK defaults. (User decision, 2026-07-29.)
4. **GUI toolkit is FLTK**, not VCL/Qt. Event-loop and dialog-modality details
   will differ internally; user-visible behavior should not.
5. **UI language is English**, not Japanese. Captions are translated (mapping
   table in `docs/05-gui-layout.md`); positions, sizes, and behavior still
   match the original. (User decision, 2026-07-29 — user does not read
   Japanese.)
6. **Settings are stored as `<exe-dir>/isobar.ini`, not `kgfax.ini`.**
   (Location resolved 2026-07-30 per user request: next to the executable,
   like the original. Filename changed 2026-08-01, user decision.)
   Sections, value encodings and the `[Set]`/`[Wave]`/`[Form]`/`[Dir]` keys
   still follow the original schema, but the file is no longer *shared*
   with the original program, because two keys have diverged in meaning
   (#16). Both programs parse either file without complaint, so a shared
   file would let each silently mis-tune the other.
   **The six tuning keys are also renamed** (user decision, 2026-08-01,
   while `isobar.ini` was still unreleased and renaming was free):
   `Sync2Thre`→`DarkThreshold`, `SyncThre`→`FallbackDepth`,
   `LReSycn`→`ReleaseAfter`, `RReSycn`→`LockAfter`,
   `SyncWidth`→`MaxJump`, `DetTime`→`ToneBlocks`. The originals are
   opaque, two are misspellings of "Sync", and two actively mislead —
   `SyncWidth` is not a width, and nothing in `LReSycn`/`RReSycn` says
   which one locks. Every one of those traps was walked into during the
   2026-08-01 audit. `docs/01` §6 keeps the original vocabulary and
   carries the mapping table; `settings_read_kgfax()` still parses it.
   On first run, if there is no `isobar.ini` but a `kgfax.ini` is present,
   `DirName`, `rpm`, `syn`, `CycleGet`, `WaveDev` and `FormX`/`FormY` are
   imported; `kgfax.ini` is never written. The whole `[Sync]`/`[Det]`
   tuning block is deliberately left at our defaults — besides #16, a
   `kgfax.ini` sitting next to the executable may have been written by an
   *older build of this program*, when `LReSycn`/`RReSycn` were swapped and
   `SyncWidth`/`DetTime` were on different scales (fixed 2026-08-01). Such
   a file is indistinguishable from the original's, and importing its
   tuning would silently produce nonsense.
9. ~~Live preview returns to the upright view when reception stops~~
   **Resolved 2026-07-30:** traced from the binary — the original NEVER
   redraws on stop; it keeps the sideways column view (and the `.syn`
   load renders the same column view). Our preview now always shows the
   column view too (docs/01 §4 "The main preview is ALWAYS this column
   view") — no longer a deviation.
10. **Auto-save falls back to the executable's directory** when
    `DirName` is unset. The original has no fallback at all
    (`DirName\YYYYMMDDHHMM.syn` with an empty DirName lands at the
    drive root on Windows — a quirk we do not replicate);
    next-to-the-exe is the sane macOS equivalent of the original's
    everything-beside-the-exe model.
11. ~~Sync period nudging.~~ **Removed 2026-07-30 (Session 5):** the
    scanner was rewritten to the original's fixed 4000-sample-grid
    cadence + phase-offset tracking model, so the period-nudging
    deviation no longer exists. Number retained to keep the sequence
    stable (other entries cross-reference #12–#15 by number).
12. **Image export is BMP-only, no clipboard output, native file
    dialog.** The original's Form6 has an "output to clipboard"
    checkbox and its Form3 dialog offers BMP or JPEG. FLTK has no
    image-clipboard support and no JPEG writer, so Save image always
    writes a 24-bit BMP via the native save dialog (kind + orientation
    options kept, in a Form6-replica dialog). For the orientation
    mapping/rotation see #14.
13. **Form8's drive/dir/file listboxes are a native directory
    chooser.** The original's auto-save settings dialog has Win31-style
    DriveComboBox/DirectoryListBox/FileListBox controls. We show a
    read-only path field + "Browse…" button opening the native macOS
    directory chooser (same style as #12); everything else in the
    dialog (CycleGet checkbox, image-size radios, OK/Cancel, positions)
    matches the original.
14. **Image-export orientation labels are swapped from the original.**
    The original's Form6 maps 横 (Land.) = raster as-is, 縦 (Port.) =
    90°-rotated export (docs/01 §4 "Save image"). The user found that
    backwards (2026-07-31): choosing "Land." produced a portrait-shaped
    file and vice versa. The port maps Land. = 90° CCW rotated, Port. =
    as-is, so the label matches the shape of the saved image. (The CCW
    direction itself is user-validated: CW exported sideways-transmitted
    JMH charts upside down.)
15. **Auto-save format is a radio choice, not the original's filter
    dropdown; JPEG is dropped.** The original's Form8 picks the auto-save
    output format with a Win31 `TFilterComboBox` listing `*.syn` / `*.bmp`
    / `*.jpg` (docs/01 §4 "Auto-save"). FLTK has no equivalent combo, so
    the port replaces it with a two-button radio group (.syn / .bmp).
    The `.jpg` (index 2) branch is not ported — it needs the VCL JPEG
    unit and we have no JPEG writer — so the port offers syn/bmp only.
    The choice is runtime-only like the original's (the filter index has
    no ini key); the size radios are enabled only when .bmp is selected,
    matching the original's `FilterComboBox1Change`. Both branches match
    the original's save semantics exactly: `.syn` clears the buffer
    afterward, `.bmp` does not (the asymmetry is in the trace, preserved
    faithfully).
