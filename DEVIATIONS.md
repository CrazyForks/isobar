# DEVIATIONS — deliberate differences from original KG-FAX v1.1.3

Everything here is an **intentional** departure from the original program.
If a difference is not listed here, it is a bug to fix, not a feature.
Seeded 2026-07-29 per `docs/04-decision-guide.md`.

## DSP / architecture

1. **Decoder runs on a worker thread, not the GUI thread.** The original
   serializes DSP through `TThread::Synchronize` onto the GUI thread, which is
   the cause of the window-animation warning in the original readme. We keep
   the DSP math identical but move it off the UI thread.
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
6. ~~Settings file lives at `~/kgfax.ini`~~ **Resolved 2026-07-30 (user
   request):** the ini now lives next to the executable, exactly like the
   original's `<exe-dir>\kgfax.ini` — no longer a deviation. Section/key
   names and value encodings follow the original schema exactly
   (including the `LReSycn`/`RReSycn` typos).
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
