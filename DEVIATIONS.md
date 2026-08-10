# DEVIATIONS — deliberate differences from original KG-FAX v1.1.3

Everything here is an **intentional** departure from the original program.
If a difference is not listed here, it is a bug to fix, not a feature.
Seeded 2026-07-29 per `docs/04-decision-guide.md`.

## DSP / architecture

1. **Decoder runs on a worker thread, not the GUI thread.** The original
   serializes DSP through `TThread::Synchronize` onto the GUI thread, which is
   the cause of the window-animation warning in the original readme. We keep
   the DSP math identical but move it off the UI thread.
16. **Sync detection is our own algorithm, so `Sync2Thre` keeps our
    default (96, not the original's 20); `SyncThre` is adopted (30).**
    Traced 2026-08-01, **re-traced and partly corrected 2026-08-02**
    (docs/01 §3.2(7)(8)). The original binarises the **raw** video at
    `Sync2Thre` and scans one bright run, one dark run and at most one more
    bright run, then checks the period is ~4000 and the **bright** total is
    100..400 samples. Its fallback minimises a boxcar mean over the `syn`
    window, subject to the 8 samples after that window averaging above 128,
    and accepts the result if the mean is below `SyncThre` — an absolute
    bound, not a dip depth — and if it lies within `SyncWidth` of the
    previous position.
    Ours instead thresholds an 8-sample moving average, collects *all*
    candidate edges and chain-matches them across lines, validates the
    fallback on dip depth below the local mean, and has no trailing-bright
    test.
    **The period/pulse windows are inclusive where the original's are
    strict** (noted 2026-08-06, on the Ghidra re-verification): the
    original rejects a period ≤3980 or ≥4020 and a pulse ≤100 or ≥400;
    ours accepts 3980..4020 and 100..400 inclusive. One sample at each
    boundary, and our edge metric (the moving average) is not the
    original's binarised video anyway, so the exact endpoints do not
    transfer 1:1.
    **Two S23 claims here were wrong and are withdrawn**: that the
    original's video is inverted relative to ours (it is not — both put
    1500 Hz at 0 and 2300 Hz at ≈251), and that its shape check is a
    general sync detector. It is a **phasing-preamble detector**: a valid
    line is almost entirely dark with one bright pulse of 100..400 samples,
    which describes the black-with-white-pulse preamble, not a picture
    line. Neither fixture available in S23 contained a preamble, which is
    what sent that reading astray. On the S25 fixture, read literally with
    no inversion, 64 of 66 phasing lines validate at a threshold of 96+ and
    0 validate inverted at any threshold.
    **`Sync2Thre` stays ours** (`dark_th` = 96, not 20): the original's 20
    is defeated by small bright blips in our video's noise floor, and 96 is
    exactly what makes the original's *own* check validate 64 of 66 phasing
    lines. Its shape check also spends a normal chart entirely in the
    fallback (0/1851 lines shape-validated on an off-air picture recording)
    where ours locks directly — 1713/1851 locked with 0 coasted, against 122
    locked and 1550 coasted when fed the original's two values.
    **`SyncThre` is now adopted, at the original's value.** S25 ported the
    original's fallback tracker (`fallback_edge` in `core/syncscan.cpp` and
    its twin in `core/live.cpp`), which computes the same quantity the
    original does — a boxcar mean over the binarised video — so its number
    transfers where the old dip-depth formula's could not. It lives in
    `SyncParams::fb_mean` at **30 = `SyncThre`**, independently measured as
    the best value on all three fixtures. Our dip-depth `fb_thresh` (10)
    survives as a **second chance**, tried only when the ported test
    declines: the ported test alone is more selective but less available,
    and on its own it coasts through 45 picture lines of
    `jmh-phasing-16k.wav` that the dip-depth search corrects. One deliberate
    departure in the port — the original gates on the samples *after* the
    boxcar and so anchors the dark→bright edge, while its own shape check
    anchors the bright pulse, two reference points a whole `syn` window
    apart that its jump guard then rejects. We gate on the samples *before*
    the window, so the raw position is the bright→dark edge — the same raw
    reference our shape check starts from; both are then refined by
    `sync_anchor()` (the anchor paragraph below), so the phase is published
    at one consistent reference: same mechanism, one consistent reference
    (docs/01 §3.2(8)).
    The ini's `FallbackDepth` drove the dip-depth second chance until
    2026-08-04; it now drives `fb_mean`, the original's own meaning, at
    the original's default 30 (the "Sync detect" combo in the Details
    dialog edits it). `fb_thresh` stays hard-coded at 10 with no ini key.
    The other ten ini defaults are adopted exactly.
    **A step in the sync position is followed on confirmation, and every
    other fallback result is rate-limited rather than rejected** (added
    2026-08-02; `sync_step_lock()` and `sync_slew()` in `core/syncscan.h`).
    The original's `SyncWidth` guard
    *rejects* a fallback position further than `MaxJump` from the previous
    one. Ours cannot, for two reasons.
    First, the sync strip is rotated to index 0 — the seam where a line
    wraps — so a phase error does not shift a line, it **splits the strip**,
    half at each end, which is what the user sees when the Sync corr LED
    lights.
    Second, **real transmissions step further than `MaxJump`**: measured
    directly, the 12 kHz off-air recording's true sync moves from 2131 to
    1969 between one line and the next and holds the new position for the
    rest of the reception, and `jmh sample.wav` does the same at line 930
    where a new transmission starts. Rejecting means crawling toward the new
    position for a dozen lines, every one of them split — measured, it
    raised mis-phased picture lines on the off-air recording from 36 to 211.
    So a whole-line search runs first, accepted only when the pulse is
    unambiguous (nearest rival ≥ 300 samples away and ≥ `DarkThreshold`/2
    brighter, and the dark run it sits in is 100..400 samples wide — the
    bound the shape check already uses, which is what keeps a chart's wide
    black margin out) *and* the **next** line agrees with it. A hand-placed
    sync position (`nudge_phase`) suppresses the search entirely until the
    decoder earns a shape lock of its own, since overriding the strongest
    sync-looking pulse is exactly what that feature is for. That is **one line of
    lookahead**, and it is deliberate: confirming forwards instead of
    backwards lets the step be followed on the very line it starts on,
    which is the line whose strip would otherwise be split. It costs the
    live path half a second of latency — a completed line is held until the
    line after it arrives — and adds `LiveScan::finish()`, without which the
    last line of a reception would never be emitted. Everything that is not
    a confirmed step is rate-limited instead: `MaxJump`/4 samples on the
    first line, doubling per consecutive line agreeing on the direction.
    Line-to-line strip movement over 10 px, on the three full recordings:
    `jmh sample` 54→13, `FAXSignal` 25→18, off-air **56→8**; mis-phased
    picture lines off-air **36→9**. Guarded by `slew-test`.
    **A picture-heavy image needs absolute darkness, not position or
    relative contrast, to tell a sync pulse from the picture** (added
    2026-08-03 for a KiwiSDR himawari reception; `sync_dark_floor()` =
    `DarkThreshold`/3, applied to the lock chain's start, to the whole-line
    rescue as an alternative to its rival-margin test, and to the dip-depth
    fallback before it may slew the phase). Cloud edges repeat line to line
    closely enough to sustain a lock chain of their own, and the edge list
    is in position order, so a picture chain earlier in the line beat the
    real pulse: the decoder locked 339 samples off on its first lock and
    could not recover, since re-acquisition is confined to the phase it
    already holds. The original has no equivalent test — its own detector
    never sees a picture line at all (it is a phasing-preamble detector, as
    above), so the question does not arise for it.
    **The line phase is anchored on the darkest window of the sync pulse,
    not on its leading edge** (added 2026-08-03; `sync_anchor()` in
    `core/syncscan.h`, applied at every point where a detected position
    becomes the phase). The original — and
    every detector here until now — publishes the one sample where the video
    first crosses `DarkThreshold`. That sample moves with noise and with
    whatever picture abuts the pulse; the whole pulse is far bigger
    evidence, and a window as wide as the pulse straddles both its edges, so
    their noise partly cancels. Measured line-to-line wobble of the same
    pulses: leading edge median 2.2 samples / 90th percentile 42.2 on the
    himawari reception, darkest window **0.8 / 3.8**. It is bounded so it
    can never wrap the strip (see the header comment); where the pulse
    cannot be seen whole — a chart's black margin merged with it — the
    published position stands and behaviour is exactly as before.
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
   with the original program, because one key has diverged in meaning
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
12. **Image export is BMP or PNG, no clipboard output, native file
    dialog.** The original's Form6 has an "output to clipboard"
    checkbox and its Form3 dialog offers BMP or JPEG. FLTK has no
    image-clipboard support and no JPEG writer, so Save image writes
    via the native save dialog (kind + orientation options kept, in a
    Form6-replica dialog). For the orientation mapping/rotation see #14.
    **Amended 2026-08-09:** the dialog gained a Format group (BMP /
    PNG — the window grew downward; the original groups keep their
    Form6 coordinates). PNG is written as true 8-bit grayscale when the
    monotone palette is active (much smaller), and as RGB when a color
    palette is applied — the same pixels the BMP would carry.
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
    the port replaces it with a radio group (.syn / .bmp / .png).
    The `.jpg` (index 2) branch is not ported — it needs the VCL JPEG
    unit and we have no JPEG writer. The `.png` choice is our addition
    (2026-08-09, user request): it always writes 8-bit **grayscale**,
    even when a color palette is active — auto-save PNG is the small
    archival format. The choice is runtime-only like the original's (the
    filter index has no ini key); the size radios are enabled for .bmp
    and .png, matching the original's `FilterComboBox1Change`. The .syn
    branch matches the original's save semantics exactly (clears the
    buffer afterward); .bmp does not clear it (the asymmetry is in the
    trace, preserved faithfully), and .png follows the .bmp behavior.

17. **The receive buffer grows past the original's 2280-line clamp, up
    to 4560 lines.** The original's image buffer is a fixed 1500×2280
    (19 min @ 120 rpm); a fuller buffer is clamped by dropping the
    oldest line (`dword_4F25BC > 2279`, docs/01 §4). Real charts can run
    longer — XSG (Guangzhou) broadcasts decode to ~2755 lines — so the
    port keeps receiving up to `FaxImage::HARD_MAX_LINES` = 4560 (2×)
    before the same drop-oldest clamp applies. The extension is active
    only when auto-save is off or armed on the `.bmp` filter; with
    auto-save on `.syn` the original 2280 behavior is kept (CycleGet
    saves+clears, otherwise drop-oldest), so an armed `.syn` auto-save
    can never hit an oversized buffer. Every `.syn` output stays
    KG-FAX-compatible: `.syn` writing still refuses >2280 lines, and the
    manual "Save data" on a longer image offers to save just the first
    2280. Display-side, the live preview and the zoom-0 render widen
    past 760 columns (the parent scroll view handles overflow), the
    zoom-1/2 pan ranges grow with the line count, and printing an
    extended image fills the whole page (the 2280-line ruler only
    shrinks partial receptions).

18. **Load data also accepts BMP and PNG images** (added 2026-08-09,
    user request). The original loads `.syn` only. The port's
    Load-data menu takes `.syn` / `.png` / `.bmp` (one flat item per
    type): an image file is reduced to grayscale (ITU-R 601 luma for
    color input), rotated 90° CW into the buffer's sideways raster —
    the exact inverse of the "Land." export's CCW rotation, so an
    exported or auto-saved chart loads back the way it looked; a
    raster-orientation ("Port.") file conversely loads rotated, there
    is no way to tell the two apart — then each row is box-resampled to
    the fixed 1500 px line width when the file is narrower or wider,
    and the palette resets to monotone — a plain image carries no
    color mode, unlike a `.syn` header. PNG input is limited to 8-bit
    non-interlaced grayscale/RGB/RGBA; BMP input to uncompressed
    24/32-bit. Unsupported variants fail with a clear error message.

19. **WMO inverted phasing is acquired, and the phase is then HELD
    through pulse-free pictures** (added 2026-08-09, user request: a
    VMW/Wiluna off-air recording was undecodable). The port's sync
    machinery was built around the JMH-style per-line black pulse in a
    white signal, which JMH/JSC/XSG keep sending through the picture.
    WMO-standard phasing is the inverse — a black signal with one white
    pulse per line (IOC 576: 25 ms = 5%), sent ≥30 s before each chart —
    and some stations, VMW being the known case, send **no per-line
    pulse during the picture at all**. On such a broadcast the
    black-pulse detector never locked (0.9% of lines on a 646 s VMW
    recording) and the fallback then wandered the phase across picture
    content (133 false corrections), shredding the chart into
    mirrored-looking fragments.
    The port now mirrors the normal acquisition with the polarity
    flipped (`LiveScan::try_inv_lock` in `core/live.cpp`, and the
    bright-run branch of `SyncRuns`): bright runs of sync-pulse width chain
    across lines, gated to black-dominant lines (`sync_line_dark`) and a
    white floor (`sync_bright_floor`), both in `core/syncscan.h`. The
    anchor is the pulse's LEADING edge (`SYNC_INV_OFFSET` = −196 from the
    trailing edge it publishes; the pulse is 196 samples, the WMO 25 ms).
    Where the seam sits is a free choice here in a way it never is for a
    station with per-line sync — that station's phase *is* its sync pulse,
    while a WMO-phasing picture carries nothing — and the one thing it
    must do is fall off the page. VMW's chart covers about two thirds of
    the drum: anchoring where JMH anchors (+58; measured at +61 and +60
    past the trailing edge on `jmh sample.wav` and `jmh-kiwi-testchart
    .wav`, over 118 and 55 preamble lines) put the seam 21 px inside the
    page and wrapped the masthead's first letter and the panel border to
    the far right. The leading edge puts it 75 px into the blank margin,
    and the chart lands whole with 78 px clear on the left and 425 on the
    right.
    After the lock, **inv_mode holds the phase**: no fallback, no
    release — the picture carries nothing to track, so coasting is
    exactly right; the next chart's phasing re-anchors through an
    inverted shape match.

    **The hold runs at the measured line period, not at 4000 samples.**
    A station without per-line sync gives the decoder exactly one chance
    to measure the line rate, and 4000 is not it: a KiwiSDR's 12 kHz
    stream is really ~12000.96 Hz, which is 3999.68 samples per line
    (least squares over 57 preamble pulses of the VMW recording, residual
    max 1.7 samples). Held at 4000, the phase walked 0.32 samples per
    line — 412 samples = 154 px of shear across one chart, with the
    chart's own content cut at the seam and no vertical border straight
    (best column dark on 24% of its rows). So the preamble anchors feed a
    least-squares fit (`SyncInvDrift` in `core/syncscan.h`, shared by both
    paths) and the coasting phase advances by the fitted period. On the
    same recording the shear is gone: the panel border is dark on 100% of
    its rows, residual drift 0.003 px/line. Stations WITH per-line sync
    never showed this — their tracking absorbs the same clock error line
    by line — which is why it survived until a no-sync station arrived.

    **And a dropout is followed off the picture's own content.** A held
    phase is only as good as the stream feeding it: a networked SDR drops
    audio, and this recording loses 60-80 ms three times inside one chart
    (lines 686, 911, 952 — no silence in the WAV, the samples are simply
    gone). A station with per-line sync shrugs that off, `sync_step_lock`
    re-locks on the line it happens; here nothing would ever notice, and
    the chart's panel border walked 972 → 744 → 507 → 316 px across the
    page. So `sync_content_step` correlates each picture line against the
    previous one over ±800 samples (decimated by 4, refined at full rate)
    and takes the lag when it is more than the drift model would ever
    move, matches (0.35), beats no-move (0.10) and every other lag
    (0.10), and the NEXT line agrees — the same lookahead `sync_step_lock`
    uses. One extra gate does the real work: both lines must carry ink
    (RMS 32 of the mean-removed profile). Without it two false steps in
    the masthead band passed every correlation test and tore the Bureau's
    logo in half; the measured table is in `core/syncscan.h`. Corrected,
    the border runs straight down 500 lines at one column and the whole
    chart — both map panels, legend, time-zone box — comes out whole.

    Three guards keep it in its lane: acquisition only fires
    before the stream's first lock (once any phase reference exists, the
    established convention owns it — a JMH preamble is the same
    white-pulse-in-black shape, and inv-locking it would throw away a
    phase that was already right); a re-anchor mid-stream needs
    `SYNC_PHASING_CONFIRM` = 8 consecutive black-dominant lines, since a
    real preamble is at least 60 of them while a chart's own dark bands
    are 2 and 4 lines on this recording — and each of those re-anchored
    the phase by up to `search_win`, stepping the image sideways; and the
    escape out of inv_mode (a JMH-style station taking over the
    frequency) requires a black-pulse chain to confirm at one phase for
    `SYNC_ESC_CONFIRM` = 20 lines and never fires on black-dominant
    lines — VMW's end-of-chart ruler band chains and confirms exactly
    like a sync pulse (measured false escapes 12 lines long, and one
    396 px off). Live reception mirrors the batch path byte for byte
    (`invphasing-test`, fixture `vmw-phasing-12k.wav`, which also injects
    4- and 16-line dark bands to check the re-anchor gate both ways).
    Note the
    original's own shape check is itself a phasing-preamble detector
    (#16), so this brings the port's acquisition closer to the
    original's, with a different picture strategy: the original
    fallback-tracks under its MaxJump rejection, we hold.
