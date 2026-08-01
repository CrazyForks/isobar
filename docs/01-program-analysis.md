# KG-FAX v1.1.3 — Functional Specification

This document records the functional behavior of KG-FAX v1.1.3 (K.G, 2009),
a Japanese freeware HF weather-fax decoder for Windows. It is a **specification**:
it describes what the program does (algorithms, constants, file formats, GUI
behavior) so an independent implementation can be written from it. It is not
derived from any published source; the facts below were established by
reverse-engineering analysis of the binary, re-derived against the public
WEFAX standard (WMO-No. 386, *Manual on the Global Telecommunication System*,
Vol. I, Part III, §5) where applicable, and cross-checked against the
program's observed behavior.

Symbol names of the form `sub_XXXXXXXX`, `dword_XXXXXX`, `unk_XXXXXX` are
IDA/Hex-Rays output (auto-generated from addresses), not the original
identifiers; they are kept here as stable anchors for the analysis. See
`NOTICE` for the provenance and interoperability statement.

## 1. What the program is

KG-FAX decodes shortwave (HF) weather-fax broadcasts (WEFAX, emission J3C)
from soundcard audio. It targets the standard WEFAX signal (WMO-No. 386
Part III §5; section numbers below refer to it):

- FM sub-carrier: **1500 Hz = black, 2300 Hz = white**, 1900 Hz centre
  (§5.5.1). This is the sub-carrier-FM case, carried over SSB — hence the
  J3C designator. The direct-FSK case (§5.5.2, f₀ ±400 Hz on HF) is a
  different transmission mode and is not what soundcard decoders see.
- Drum speeds **120 rpm** (0.5 s/line) and **60 rpm** (1.0 s/line); the
  standard also defines 90 and 240 rpm (§5.1.5), which KG-FAX does not offer.
- IOC 288/576 implied by the 1500-px line width (no explicit IOC constant);
  the standard's values are 576 or 288 (§5.1.2).
- IOC-selection ("start") tone **300 Hz**, stop tone **450 Hz** (detected as AM
  on the demodulated brightness signal). Per §5.2.2.1 the 300 Hz tone selects
  IOC 576 and **675 Hz** selects IOC 288 — KG-FAX detects only 300 Hz, i.e. it
  is IOC-576-only. Per §5.2.5.1 the 450 Hz stop burst is followed by 10 s of
  continuous black, which KG-FAX does not use as a confirming condition.
- Phasing signal (§5.2.3.1): 30 s of alternating white/black at 2.0 Hz for
  120 rpm (1.0/1.5/4.0 Hz for 60/90/240). KG-FAX locks via its own sync-pulse
  search rather than decoding the phasing signal explicitly.
- Sync pulses: black pulse at line start; readme lists stations with 8/10/20/45 ms
  pulse lengths.

From the readme (Shift-JIS): input 22.05 kHz / 16-bit soundcard; features include
high-res reception, sync, zoom/pan, rotation, printing, false-color palettes,
auto-record/auto-save. Target OS was Windows Vista/XP. Author "K.G", 2009-07-08.

## 2. Overall structure

- Compiler: **CodeGear C++Builder 2007** (VCL + Dinkumware STL).
- 1,388 functions in the binary. Application code is a small fraction of that;
  the bulk is statically linked VCL/CRT/STL (`Sysutils::`, `Classes::`,
  `Controls::`, `Graphics::`, `Printers::`, `atan2`, `sprintf`, …).
- Entry: `WinMain` creates 10 VCL forms, then `TApplication::Run`.
- Units:
  - `Recoding` — **TForm1 (main form, holds all DSP) + TWaveInThread**.
  - Form2: progress dialog ("処理中...", buffer-clear etc.).
  - Form3: file open/save dialog (`.syn` / `.bmp` / `.jpg`).
  - Form4: sync-parameter settings (詳細設定).
  - Form5: color/gradation dialog (色処理の設定).
  - Form6: bitmap kind selection (ビットマップ種類選択, for image export).
  - Form7: received image vertical view (受信画像縦表示) — ORPHANED: the
    global `Form7` is only referenced by form creation, TForm2_FormShow
    positioning, and Unit7's own handlers; no button ever opens it
    (verified 2026-07-30). The Vertical button's real behavior is
    Button8's in-place rotate — see §4 "Rotate".
  - Form8: auto-save settings (自動保存の設定).
  - Form9: input device select (入力デバイスの選択; enumerates via
    `waveInGetNumDevs`/`waveInGetDevCapsA`; buttons launch
    `SNDVOL32.EXE /RECORD` or `rundll32 … mmsys.cpl`).
  - Form10: recording device change popup (録音デバイス変更).
  (Form captions/coordinates corrected against the DFM extraction in
  `05-gui-layout.md` — that file is authoritative for forms.)
- `TForm1` instance is **~6.84 MB**: both 1500×2280 image buffers are embedded in
  the form object (offsets +1098 and +3421098), not heap-allocated.

## 3. DSP chain

### 3.1 Audio capture

- Format built in Form1 ctor: **22050 Hz, 16-bit, mono PCM** (`dword_4DFE50 =
  22050`; `dbl_4DFE5C = 1/22050`).
- `waveInOpen(dev or WAVE_MAPPER, …, sub_402904, CALLBACK_FUNCTION|WAVE_ALLOWSYNC)`.
  Allocates **32 WAVEHDRs × 4410 bytes** (100 ms each), queues all, `waveInStart`.
- Callback `sub_402904`: on `WIM_DATA` posts **WM_USER+1** to Form1;
  Form1's message handler spawns a `TWaveInThread` whose Execute does
  `TThread::Synchronize(sub_402A18)`. **All DSP runs serialized on the
  GUI thread** — this is why the readme warns Windows window animations break sync.

### 3.2 The decoder — `sub_402A18`

Runs once per 2205-sample (100 ms) block:

1. **int16 → double** conversion into `dbl_55D7F4[2205]`.
2. **51-tap bandpass FIR** (taps at `unk_4D32E4`, symmetric, Σ≈0, center tap
   0.1996): isolates the 1500–2300 Hz subcarrier, rejects DC.
3. **Quadrature FM demod (Hilbert + atan2)**:
   - Second 51-tap FIR (`unk_4D3614`, antisymmetric, center 0) = Hilbert
     transformer → Q. I = delay-line center tap (25-sample group delay).
   - `phase = atan2(Q, I)`; phase difference with 2π unwrap.
   - Reference subtract: `−1.425 + 0.996 + 0.00156` ≡ **−0.42744 rad =
     −2π·1500/22050** — i.e. referenced to the 1500 Hz black carrier.
   - Scale **×1102.25**: 2300 Hz → ≈251; later clamped 0–255.
4. **51-tap lowpass FIR** (`unk_4D347C`, symmetric, Σ≈1.015) smooths the video
   signal.
5. **Start/stop tone detectors**: `sign(demod) → ±10000` fed into two
   2-pole resonators (coefficients computed in ctor):
   - 300 Hz resonator, 10 Hz BW (`dword_4DFE64=300`, `dword_4DFE6C=10`):
     `a1 = cos(300·2π/fs)·2e^(−π·10/fs)`, `a2 = −e^(−2π·10/fs)`,
     `b0 = sin(300·2π/fs)·0.1`.
   - 450 Hz resonator, same form (`dword_4DFE68=450`).
   - Output squared, lowpassed (`b={0.00019897, 0.00039794, 0.00019897}`,
     `a={1, −1.95970703, 0.96050292}`), max over block → 300/450 Hz levels.
   - Level > 1e7 for `DetTime` consecutive blocks → **auto-start** (300 Hz,
     presses record SpeedButton) or **auto-stop + auto-save** (450 Hz,
     `sub_40C858`).
6. **Decimation** 2205 → 800 samples per block (factor 1/2205) → 8000 samples/s
   into `byte_4E89B8`. One 120-rpm line = 5 blocks = **4000 samples = 0.5 s**.
   `dword_4F25C4` (1 or 2, from rpm combo) makes 60-rpm lines 4000 samples @
   4000 samples/s (10 blocks × 400).
7. **Sync pulse detection**: threshold a 4500-sample window at `Sync2Thre`; find
   the black sync run; valid if period 3980–4020 samples and pulse width
   100–400 samples (12.5–50 ms @ 8 kHz).
8. **Fallback sync tracking**: when shape check fails, minimum-brightness search
   in a window of `dword_4ED894` samples around the predicted position,
   validated against `SyncThre`, with hysteresis counters
   `LReSycn`/`RReSycn` before lock/unlock transitions.
9. **Slant meter**: `drift = ((syncPos − prevSyncPos) + drift) * 0.5` EMA;
   |drift| ≤ 1 → green/yellow "locked" LED, else red (LEDs are recolored
   TProgressBars, via `sub_47F044`).
10. **Phasing/slant correction**: each 4000-sample line is circularly rotated so
    the sync edge is at index 0 (via `byte_4E9958`), then stored.

**Sync-track enable** (`byte_4ED6D0`, the Sync speed button — default ON; not
persisted to the ini): gates steps 7–9's *writes*, not step 10. Edge detection
always runs and computes its validity flag, but the detected edge position is
written to the rotation offset only when enabled; the LED/slant block and the
fallback min-brightness search run only when enabled. Line assembly is
unconditional: advance is always a fixed 4000 samples, and the line is rotated
by the offset whatever its source. So with tracking OFF the decoder freezes
the phase at its last value — no correction, no coasting, no LED updates —
but lines keep being produced. Turning the button OFF also resets the state
machine (ever-locked flag, found/lost counters) and blacks both sync LEDs;
turning it ON just re-enables (lock re-acquires through the normal warmup).
Two quirks: changing the sync-width combo re-enables tracking in software
*without* updating the button's visual state, and clicking the preview image
in phasing mode while tracking is OFF presses the button programmatically and
seeds the reference offset from the click position (manual phase align).

**Manual sync align** (the readme's 同期処理の停止と手動同期位置指定; ported
2026-08-01, S21). Two halves. (a) Release the Sync button to stop tracking
when the signal is buried in noise, so a hopeless lock stops degrading the
picture — that half was already there. (b) With tracking OFF, click the live
preview where the sync signal really is — needed when the video contains
something that merely *looks* like a sync pulse, photographs especially. The
click seeds the reference offset (`dword_4ED6C4`, the position the next
line's narrow edge search centres on) with *current edge + 8·(500−clickY)*,
sets the ever-locked flag, and presses the Sync button back down. The
preview's 500 rows are one line's 4000 samples with the line start at the
BOTTOM, so 8 samples per row and the click is measured up from the bottom;
click just *below* the sync strip, which is where the line starts.
Port: `LiveScan::nudge_phase(delta)` publishes the shift atomically and
`pump()` applies it at the next line boundary — before, and instead of, the
Sync-button transition, since re-acquiring a fresh lock would discard the
hand-placed reference. The GUI (`cb_live_click`) supplies 8·(500−y) and the
formula's screen geometry stays out of `core/`.
One deliberate departure, in `LiveScan::nudge_phase`: the wrap is a true
modulo, where the original's out-of-range branch computes `4000 − v` and so
yields a *negative* offset for v ≥ 4000 (a bug, or an IDA artifact for
`v − 4000`; either way not worth reproducing). Otherwise faithful: the
original's guard is `byte_4ED898` (set while reception runs, cleared when it
stops), matched by the port's `app->recording`, and it takes any mouse-up
with no click-vs-drag and no button test, which the port matches too.

### 3.3 Spectrum / waterfall scope (gated by `byte_4ED899`)

- 4096-point radix-2 DIT FFT (twiddles `dbl_51EAE8`/`dbl_526AE8` =
  cos/−sin(2πk/4096); bit-reversal `dword_546AE8`; tables built in ctor).
- Input: BPF output × Hann window (`dbl_54AAE8[i] = 0.5 − 0.5·cos(2πi/4096)`).
- Magnitude `20·log10(sqrt(re²+im²))` (actually `20·ln` — a quirk).
- The 100×106 scope shows BOTH views at once (verified against a running
  original, 2026-07-30):
  - **Spectrum y 0–29**: current trace + 4 fading history traces
    (newest near-white, older darker blues), gray markers at x=24/72
    (= 1500/2300 Hz on the ≈1157–2773 Hz span, 3 bins/px).
  - **Waterfall y 30–105**: black→blue→cyan→white heat map (ramp
    `sub_401E00`), one row per FFT block (~0.1 s), scrolling down.
    New rows enter at slot 0 and are hidden under the spectrum for
    their first 30 updates — they emerge at y=30 already 30 frames old.
- Clicking the scope (`TForm1_Image3MouseUp`) **pauses/resumes** it
  (`byte_4ED899` = master draw enable): paused freezes the display and paints
  a red band with a white label over y≈96–105. It is NOT a spectrum/waterfall
  mode toggle (earlier versions of this doc had that wrong).

## 4. Image pipeline

- **Line → pixels**: rotated 4000-sample line → 1500 px via `line[8*i/3]`
  (2.667:1), written to main buffer at `TForm1+1098 + 1500·lineNo`.
  Buffer **1500 × 2280 bytes** (max line 2279; 2280 lines = 19 min @ 120 rpm).
  Second identical work buffer at `TForm1+3421098` for zoom/rotate.
- **Live preview**: sideways during reception — each line becomes a 500-tap
  column (taps = 8-sample averages of the 4000), and **one column is
  plotted per 3 lines** (3-line average → 1/3 scale in both axes, aspect
  preserved). Columns are drawn **flipped: y = 499 − tap**, i.e. the line
  start (sync edge) sits at the BOTTOM (confirmed 2026-07-30).
  The write cursor advances left→right (~758 px max, then the bitmap
  scrolls), with cyan "Line# n" HUD + progress cursor.
  The stored image keeps the sync pulse as its leftmost pixels — the
  sync strip is part of the image.
- **The main preview is ALWAYS this column view** — it never shows the
  raster as rows (verified 2026-07-30 against the binary + original
  screenshot):
  - On reception STOP (`TForm1_Button2Click`) the bitmap is NOT redrawn:
    the original only draws an end marker (font + TextOut at the
    frontier) and re-enables the buttons. The column view stays as it was.
  - On `.syn` LOAD (`TForm1_Button4Click`) the whole preview is
    re-rendered from the raw buffer as columns: screen col j =
    lines 3j..3j+2 averaged (offsets 4500·j + 1500·v), screen row k =
    pixels 3·(499−k)..+2 averaged, through the palette. Identical
    geometry to the live view.
  - Rotated viewing happens via the Vertical button (Button8's IN-PLACE
    90° rotate of the buffer, re-rendered in this same column view) or
    the 縦 export orientation. (Form7, a separate upright-view window,
    is orphaned dead code in the original — nothing opens it.)
- **Color mapping** `sub_401ADC`: invert is applied to the gray value FIRST
  (`v = 255 − v`), then the palette. Modes: 0 = monotone gray; 1 =
  blue→cyan→green→yellow→red (no Form5 radio selects it — only reachable
  via a `.syn` saved with mode 1; window background fill is blue instead
  of black in this mode); 2 = "blue ray" black→blue→cyan→white;
  3 = "color temperature" 6-segment rainbow
  black→blue→cyan→green→yellow→red→white. `sub_401D88` writes a 24-bit
  scanline pixel. Mode (`TForm1+1048`) and invert (`+1052`) are NOT in
  the ini; they persist only in the `.syn` header (mode byte, negative
  flag) and are restored on load. The raw buffer always stores grayscale;
  colorization happens at every presentation/export point (live line
  render, zoom views, `.syn`-load redraw, export, print).
- **Form5** (color dialog): radios モノトーン→mode 0, 色温度→mode 3,
  ブルーレイ→mode 2 (any other mode shows as モノトーン); 反転 checkbox
  = invert. Pending state edits a copy; the sample strip (top = strong,
  bottom = weak) previews the pending palette live. OK commits and sets
  a changed flag; Cancel reverts fully.
- **Button10** (色処理 Color): shows Form5 modally; on OK (and an image
  exists) re-renders the whole displayed image from the raw buffer
  through the new palette, honoring the current zoom mode. (Earlier
  versions of this doc wrongly called this the zoom-restore button; the
  zoom-reset button is unidentified.)
- **Form7** (受信画像縦表示 "Vertical view"; traced 2026-07-30):
  **ORPHANED in the original** (verified 2026-07-30): no button opens it
  — the global `Form7` is only referenced by form creation,
  TForm2_FormShow positioning, and Unit7's own handlers. The actual
  縦表示 behavior is Button8's in-place rotate (see "Rotate" below).
  Kept here as dead-code documentation; not ported.
  Modeless 502×602 window, one 500×600 image at (1,1), centered over the
  main preview on show (Top/Left clamped ≥ 0). Shows the raw buffer in
  RASTER orientation (x = pixel within line, y = line number) through
  the current palette; buffer past the last received line is black. Zoom
  state `dword_561D44` ∈ {0,1,2}, reset to 0 on show. All renders
  average per line first, then across lines (integer division at each
  step):
  - Mode 0 = 1/3 scale: screen (j,i) = avg of lines 3i..3i+2 at pixels
    3j..3j+2 (per-line /3, then /3). Line offset `dword_561D60`, clamp
    [0,160] → max line 2279. (The re-render loops index from the right
    via `499−ii`; the geometry is identical to FormShow's.)
  - Mode 1 = 1/2 scale: screen (sx,sy) = 2×2 avg of lines
    2·(off48+sy)+{0,1} at pixels 2·(250−off4C+sx)+{0,1}; off48
    (`dword_561D48`) ∈ [0,538], off4C (`dword_561D4C`) ∈ [0,249]
    (virtual render 1140 rows × 750 cols).
  - Mode 2 = 1:1: line = off48+sy, pixel = 1000−off4C+sx; off48 ∈
    [0,1679], off4C ∈ [0,999] (render 2280×1500).
  - Plain click (|drag| < 10 px both axes): LEFT = zoom in
    0→1→2→close. Centered on the click: 0→1 stores v50 =
    clamp(clickY,253,613), v54 = clamp(499−clickX,166,333), then off48 =
    clamp(1.5·clickY−380, 0,539), off4C =
    clamp(1.5·(499−clickX)−250, 0,249); 1→2: off48 =
    clamp(3·v50+2·clickY−1140, 0,1679), off4C =
    clamp(3·v54+2·(499−clickX)−750, 0,999) (v58/v5C = off48/off4C).
    The clicked source point lands at screen (≈248,380) — below/left of
    the exact viewport center (the −380/−250 constants). Quirk: the 0→1
    transition ignores the mode-0 pan offset. RIGHT = zoom out
    2→1→0→close: 2→1 recomputes off48/off4C from stored v50/v54 (clamps
    [0,539]/[0,499]); →0 resets both offsets to 0 (the mode-0 pan
    variable survives but is not re-applied).
  - Drag (|dx|≥10 or |dy|≥10, deltas = MouseDown pos − MouseUp pos):
    LEFT pans. Mode 0: v60 += dy, clamp [0,160], off48 = v60 (vertical
    only). Mode 1: v50 += ⅔·dy clamp [253,613]; v54 −= ⅔·dx clamp
    [166,333]; off48 = clamp(1.5·v50−380, 0,538); off4C =
    clamp(1.5·v54−250, 0,249). Mode 2: v58 += dy clamp [0,1679]; v5C −=
    dx clamp [0,999]; v50 = clamp((v58+300)/3, 253,613); v54 =
    clamp((v5C+250)/3, 166,333); off48 = v58, off4C = v5C. RIGHT-button
    drag closes the window.
  - Form2 progress dialog is shown during every render in the original;
    the port shows it too (`gui/progressdialog.*`), but on modern
    hardware the render is near-instant so it only flashes briefly.
- **Zoom/pan on the main preview** (`TForm1_Image1MouseDown`,
  `TForm1_Image1MouseUp`; verified 2026-07-30): only when IDLE — while
  recording/decoding a click instead adjusts the sync phase by
  8·(500−clickY) samples wrapped to [0,4000) and presses the sync
  speedbutton — manual sync align, see §3.2 (**ported 2026-08-01**,
  S21: `LiveScan::nudge_phase`, `FaxView::set_live_click_cb`,
  `cb_live_click`; `cli/manual-sync-test.cpp`). MouseDown records the position
  (`dword_4ED87C/4ED880`). MouseUp: |dx|<10 and |dy|<10 (deltas = down −
  up) = CLICK, else PAN (left button only; right-button drag does
  nothing). Zoom level `dword_4F25C0` ∈ {0,1,2}; all levels keep the
  COLUMN orientation (screen x = line axis, y = pixel axis, line start
  at bottom, 760×500 viewport) — only the sampling changes:
  - Level 0 = 1/3: screen (sx,sy) = 3×3 box average of lines
    3·sx+{0,1,2} at pixels 3·(499−sy)+{0,1,2} (per-line /3, then /3) —
    the normal column view. No pan (a drag at level 0 does nothing
    unless the rotate toggle is active).
  - Level 1 = 1/2: screen (sx,sy) = 2×2 box average of lines
    2·(d5C+sx)+{0,1} at pixels 2·(749−d60−sy)+{0,1} (per-line /2, then
    /2). Offsets d5C (`dword_4F255C`) ∈ [0,379], d60 (`dword_4F2560`) ∈
    [0,249].
  - Level 2 = 1:1: line d5C+sx, pixel 1499−d60−sy, no averaging. d5C ∈
    [0,1519], d60 ∈ [0,999].
  - LEFT click = zoom in, clamped at 2 (no re-render past the top). 0→1
    centers on the click: stores v64 = clamp(clickX,253,506), v68 =
    clamp(clickY,166,333); d5C = clamp(1.5·clickX−380, 0,379), d60 =
    clamp(1.5·clickY−250, 0,249). 1→2: d5C =
    clamp(3·v64+2·clickX−1140, 0,1519), d60 =
    clamp(3·v68+2·clickY−750, 0,999), also stored to v6C/v70
    (`dword_4F256C/4F2570`) for panning. The clicked point lands at the
    viewport center (≈(380,250)).
  - RIGHT click = zoom out, clamped at 0 (no re-render at the bottom).
    2→1 recomputes d5C/d60 from stored v64/v68 with the same formulas
    (clamps [0,379]/[0,249]).
  - PAN (drag ≥ 10 px, deltas down − up): level 1: v64 += ⅔·dx clamp
    [253,506], v68 += ⅔·dy clamp [166,333], then d5C/d60 from the same
    formulas. Level 2: v6C += dx clamp [0,1519], v70 += dy clamp
    [0,999], v64 = (v6C+300)/3, v68 = (v70+250)/3, d5C = v6C, d60 = v70.
  - Rotate-toggle interaction (a1+1096 set): panning at levels 0–2 also
    moves the rotate window `dword_4F25F0` and re-rotates the buffer.
    NOT ported: our rotate bakes pan = 0 into the image, so panning
    while rotated just pans the view offsets.
  - Zoom level resets to 0 on: reception buffer clear (all Button2
    paths), `.syn` load, Clear button, and the auto-save post-save
    buffer clear (inside `sub_40C858`).
- **Rotate** (verified 2026-07-30):
  - **Vertical button = Button8** (`TForm1_Button8Click`): IN-PLACE 90°
    rotate TOGGLE of the image buffer, not a window. If the toggle flag
    (`a1+1096`) is set: copy the work buffer back over the main buffer
    (restore) and clear the flag. Else: copy main → work buffer, zero
    main, then rotate: for iter in [0,1500), x in [0,1500):
    `main[390+iter][1499−x] = work[pan+x][iter]` (pan = `dword_4F25F0`,
    the zoom/pan offset, normally 0), i.e. `new[l][p] =
    old[1499−p][l−390]` for l in [390,1890); lines 0..389 and
    1890..2279 are zeroed, then set the flag. Both paths reset the pan
    offset to 0 and toggle the button caption between two strings
    (byte_4D3ADE / byte_4D3AE5; ASSUMPTION: 縦表示 "Vertical" ↔ 横表示
    "Horizontal" — exact strings unverified). While rotated, four
    reception controls (`a1+956/960/980/992`) are disabled. The preview
    is then re-rendered from the buffer with the usual zoom-aware render
    (zoom 0 = the 1/3 column view, same geometry as `.syn` load). With
    no image (`ArgList` line count 0 and not loaded), it just fills the
    preview with the background color — no rotate.
  - **XY flip button = Button11** (`TForm1_Button11Click`): 180°
    rotation of the received region, NOT a toggle (two clicks =
    identity; for inverted-sideband/LSB reception). N = `dword_4F25BC`
    (received line count; 0 → 2280). Copy main → work, then for v5 in
    [0,N): new line v5 = work line (N−v5) read backwards —
    `new[v5][x] = old[N−v5][1500−x]`. QUIRK (unverified, kept out of
    the port): the offsets are one line/one pixel past the clean 180
    (`old[N−1−v5][1499−x]`), so the original shifts the image by one
    line and one pixel; the port implements the clean 180. Lines beyond
    N are zeroed. Same zoom-aware preview re-render afterwards.
- **Print** (`TForm1_Button7Click`; verified 2026-07-30): opens the OS
  print dialog first (`a1+988` = TPrintDialog; cancel = no-op). Renders
  the FULL raw buffer (1500 px × 2280 lines) into a 24-bit bitmap in
  RASTER orientation: bitmap (x, y) = palette(buffer line y, pixel x);
  lines past the received count are black. If the 90° rotate toggle is
  active (`a1+1096`), it renders from the PRE-ROTATE backup (work
  buffer) — print always uses the unrotated image. Stretches the bitmap
  to fill the whole page: `StretchDIBits(0,0,PageWidth, PageHeight,
  0,0,1500,2280)` — no aspect preservation. Form2 progress dialog
  during the render (ported as `gui/progressdialog.*`, but near-instant
  here). Port note: our render covers the received lines only —
  printing the original's ~1000-line black tail is wasteful (small
  deliberate difference). **The page scale still uses the original's
  fixed 2280-line ruler**: N received lines are drawn into the top
  `N/2280` of the page (width still fills it), so the printed geometry
  is identical to the original's and the unreceived part is simply left
  blank instead of black. Fixed 2026-08-01 (S21) — before that the N
  lines were stretched over the *whole* page, printing a partial
  reception `2280/N` too tall (a 1157-line print came out ~2× stretched).
- **Files**:
  - Save `.syn`: `TForm1_Button5Click` — magic `"SynFax2"`, then mode byte,
    negative flag, 2-byte radix-255 line count (byte0 = n/255, byte1 = n%255
    — see §7), then 1500 bytes/line raw.
  - Load `.syn`: `TForm1_Button4Click` — accepts `"SynFax2"` and legacy
    `"Syn Fax"` magic.
  - Save image: `TForm1_Button6Click` — first **Form6** (bitmap kind:
    current display / 760×500 / 1140×750 / 2280×1500 = 3×3, 2×2, 1×1 render
    from the raw buffer with palette; orientation 横/縦, 縦 = 90°-rotated
    export; clipboard-output checkbox skips the file dialog), then **Form3**
    (custom file dialog; filter combo picks BMP or JPEG, extension
    auto-appended, filename defaults to empty). Port note: the rotation
    direction is not in the trace; the port rotates 90° CCW (user testing
    2026-07-31: CW gave an upside-down export of a sideways-transmitted
    JMH chart, CCW the upright one). The port also SWAPS the 横/縦
    mapping: "Land." = rotated, "Port." = as-is, so the label matches
    the output shape (DEVIATIONS #14, user decision 2026-07-31).
  - Auto-save: `sub_40C858` — the output FORMAT is chosen by Form8's
    FilterComboBox1 (filter index at `a1+888`, read via the vtable
    GetItemIndex at offset 220); it is an EITHER/OR choice, not a
    syn-then-bmp pair:
      * index 0 (`*.syn`, the default): writes `DirName\YYYYMMDDHHMM.syn`
        (magic SynFax2 + mode + radix-255 line count + 1500 bytes/line),
        then CLEARS the buffer and resets the received line count. Size
        radios are ignored on this path.
      * index 1 (`*.bmp`): renders a BMP at the Form8 size-radio scale
        (case 1 = (N+1)/3 × 500 = 760×500 3×3 avg, case 2 = (N+1)/2 × 750
        = 1140×750 2×2, case 3 = (N+1) × 1500 = 2280×1500 1:1) and writes
        `DirName\YYYYMMDDHHMM.bmp`. NO buffer clear on this path
        (asymmetry vs the syn path).
      * index 2 (`*.jpg`): JPEG via Jpeg::TJPEGImage, present only if the
        VCL JPEG unit linked — not ported.
  - **CycleGet** (掃引を再スタート = restart scan at max width,
    `[Set] CycleGet` ini key, `a1+1097`): when the preview frontier
    reaches max width (`*ArgList > 758`, i.e. the 1500×2280 buffer is
    full ≈ 2280 received lines) AND auto-save is armed AND the filter
    is `*.syn`, `sub_40C858` fires MID-RECEPTION: saves the full buffer
    then clears it, so reception continues into a fresh image. (The
    CycleGet path only exists on the syn filter; the bmp path has no
    buffer clear, so there is nothing to "restart".)
  - **Form8** (auto-save settings, 自動保存の設定): folder picker
    (drive/dir/file listboxes) + **FilterComboBox1** (syn/bmp/jpg —
    the output format; `FilterComboBox1Change` enables the size radios
    + drive controls only when bmp/jpg is selected, disabling them for
    syn), **CycleGet** checkbox (`[Set] CycleGet` ini key, +1097), and
    the image-size radios 760×500 / 1140×750 / 2280×1500 for the bmp
    render (runtime only — no ini key exists, §6).

## 5. GUI structure and handlers

Main form controls: 11 TButtons, 3 TSpeedButtons, ComboBox4 (sync-pulse-length
preset), ComboBox5 (drum speed), Image1 (500-px preview), Image3 (spectrum),
progress-bar LEDs.

- **Button2**: start/stop reception (clears 1500×2280 buffer with Form2
  progress dialog).
- **SpeedButton1**: sync-track enable (see §3.2 "Sync-track enable";
  latching toggle, `GroupIndex=3`, default Down/ON). **SpeedButton3**:
  auto-save arm — pressing the button DOWN opens Form8 modally; Form8 OK
  commits (`a1+1076` set by the dialog) + keeps the button down + arms
  auto-save (`byte_4F2599 = 1`); Cancel pops the button back up and
  disarms. Releasing the button just disarms (`byte_4F2599 = 0`, no
  dialog). **Button9**: opens Form4 settings.
  **SpeedButton2**: opens Form9. **BitBtn1**: close.
- **ComboBox5** (`TForm1_ComboBox5Change`): item 1 → 60 rpm
  (`dword_4F25C4 = 2`), else 120 rpm.
- **ComboBox4**: sync integration window
  `dword_4ED894 = 40·(i+1)/factor` samples = 5·(i+1) ms.
- **Form4** edits: LReSycn@+1016, RReSycn@+1024, SyncThre@+1020 (= 20·i+10),
  SyncWidth@+1040 (= 10·n ms), Sync2Thre@+1008, DetTime@+1044.
- **Threads**: only the audio path; decoding is `Synchronize`d onto the GUI
  thread per 100 ms block.

## 6. Config — `kgfax.ini`

Read `sub_40C224` (from ctor); write `sub_40BB84` (from
`TForm1_FormDestroy`). VCL TIniFile on `<exe-dir>\kgfax.ini`.

| Section | Key | Meaning / field offset |
|---|---|---|
| `[Dir]` | `DirName` | auto-save directory (+1072) |
| `[Sync]` | `Sync2Thre` | +1008 |
| `[Sync]` | `LReSycn` | +1016 (typo "Sycn" preserved in schema) |
| `[Sync]` | `RReSycn` | +1024 |
| `[Sync]` | `SyncThre` | +1020 |
| `[Sync]` | `SyncWidth` | +1040 |
| `[Det]` | `DetTime` | start/stop-tone integration time (+1044) |
| `[Set]` | `rpm` | ComboBox5 index |
| `[Set]` | `syn` | ComboBox4 index |
| `[Set]` | `CycleGet` | bool, auto cyclic save (+1097) |
| `[Wave]` | `WaveDev` | +1084, 0 = WAVE_MAPPER |
| `[Form]` | `FormX`, `FormY` | window position |

## 7. `.syn` file format

```
offset  size  content
0       7     "SynFax2"  (legacy: "Syn Fax")
7       1     mode byte (rpm/color mode)
8       1     negative flag
9       2     line count: byte0 = n/255, byte1 = n%255 (radix-255,
              high byte first — NOT standard little/big-endian), ≤ 2280
11      N×1500  raw image lines, 1 byte/px brightness
```

Verified against the binary (save: `idiv 0FFh`; load:
`count = byte0*255 + byte1`). On load the original fills lines N…2279
with black and groups the column view as count/3 + count%3. The legacy
`"Syn Fax"` variant has NO count field: after mode+negative it is a
fixed 2280 lines × 2000 px, stored 3:4 into 1500-px lines.

## 8. Interesting findings / quirks

- **GUI-thread DSP**: all decoding is serialized onto the VCL main loop via
  `TThread::Synchronize` — the root cause of the readme's "disable window
  animations" warning. A port should *not* replicate this (use a real DSP
  thread); it is a defect, not a feature.
- **Demod constant puzzle**: `−1.425 + 0.996 + 0.00156` nets exactly
  −2π·1500/22050; the split suggests hand-tuned "demodulation characteristic"
  adjustments. Preserve the sum.
- **Tone detection in the demodulated domain**: hard-limit brightness to ±10000
  and resonate at 300/450 Hz — elegant; avoids a second FM demod.
- **No station table in the binary**; JMH/BMF/HLL/KVM/NOJ/NMC/JJC frequencies
  exist only in `readme.txt`.
- **No explicit IOC constant**; IOC 288/576 is implicit in the 1500-px line and
  the sync acceptance window.
- Version string "1.1.3" appears only in the readme; binary title is just
  `KG-FAX` (`aKgFax`).

## 9. Constants to extract for a bit-exact port

| What | Where |
|---|---|
| BPF FIR taps (51) | `unk_4D32E4` — symmetric, Σ≈0, center tap 0.1996 (re-derivable as a windowed-sinc 1500–2300 Hz @ 22050 Hz) |
| LPF FIR taps (51) | `unk_4D347C` — symmetric, Σ≈1.015 |
| Hilbert FIR taps (51) | `unk_4D3614` — antisymmetric, center 0 |
| FFT twiddles + bit-reversal | `dbl_51EAE8`, `dbl_526AE8`, `dword_546AE8` (or recompute: cos/−sin(2πk/4096)) |
| Demod reference & scale | −0.42744 rad (= 2π·1500/22050), ×1102.25 |
| Resonator coeffs | formula (300 Hz/450 Hz, 10 Hz BW, b0 = sin(ω)·0.1) |
| Tone-detector LPF | b={0.00019897, 0.00039794, 0.00019897}, a={1, −1.95970703, 0.96050292} |
| Decimation factor | 1/2205 per block → 8000 samples/s |
| Sync validity window | period 3980–4020 samples, pulse width 100–400 @ 8 kHz |
| Image geometry | 4000 samples/line → 1500 px via `8*i/3`; buffer 1500×2280 |

Note: the FIR tap arrays live in the binary's data section. They are
re-derivable as standard windowed-sinc designs from the passband specs above
(the approach the independent implementation takes); exact per-tap values from
the original would require reading its data segment.
