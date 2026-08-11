# Option A — Cross-Platform Port (FLTK; originally Qt + RtAudio)

> **Toolkit decision (2026-07-29):** Option A was chosen, but with **FLTK**
> instead of Qt + RtAudio. Reasons: user wanted a lighter toolkit than Qt;
> FLTK uses absolute x/y widget placement (like the original VCL forms), is
> tiny, statically linkable, and is proven in this exact niche (fldigi).
> Audio capture: use RtAudio or PortAudio with FLTK (decide at GUI milestone;
> M0 needs neither). The `core/` DSP library and the milestone structure below
> apply as written; only the `gui/` and audio-backend specifics shifted from Qt
> to FLTK (the architecture tree was updated to match the implemented layout).

Goal: a window-for-window, bug-for-bug replica of KG-FAX v1.1.3 running natively
on **macOS, Linux, and Windows** from one C++ codebase.

## Why this option

- Qt's `QWidget` model maps almost 1:1 onto the original VCL forms — the app is
  10 simple 2009-era dialogs; `TForm1_Button2Click`-style handlers become slots.
- Pixel-buffer blitting (the app constantly writes scanlines) is natural with
  `QImage` + `scanLine()`; drag-zoom via `QRubberBand` is nearly free.
- RtAudio/PortAudio covers CoreAudio + ALSA/Pulse/JACK behind one API.
- A Qt build reproduces the *flat 2009 VCL look* more faithfully than AppKit —
  which matters for a window-for-window replica.
- Linux/BSD support costs ~10–20% extra (packaging/QA), not a rewrite.

Qt licensing: LGPL — fine with dynamic linking (the normal case). Alternative:
wxWidgets (permissive license, clunkier API, less faithful widgets). **Default
recommendation: Qt 6 + RtAudio.**

## Architecture

```
isobar/
├── core/                    # framework-agnostic, C++17; only dep is zlib (PNG)
│   ├── decoder.{h,cpp}      # BPF → Hilbert/atan2 demod → LPF → decimate
│   ├── filters.{h,cpp}      # FIR BPF/LPF (coefficients re-derived, not copied)
│   ├── syncscan.{h,cpp}     # sync detect + fallback tracker + line rotate
│   ├── live.{h,cpp}         # streaming scan (cadence+phase model)
│   ├── tonedetect.{h,cpp}   # 300/450 Hz resonators (docs/01 §3.2(5))
│   ├── palette.{h,cpp}      # gray / 2 gradients / rainbow / negative
│   ├── bmpfile.{h,cpp}      # BMP write (Form6 path) + read (image input)
│   ├── pngfile.{h,cpp}      # PNG write (gray/RGB) + read (→ luma), on zlib
│   ├── synfile.{h,cpp}      # .syn read/write ("SynFax2" + legacy "Syn Fax")
│   ├── wavfile.{h,cpp}      # WAV reader (any rate ≥ 6000 Hz → 22050,
│   │                        #   PCM incl. WAVE_FORMAT_EXTENSIBLE)
│   ├── resample.{h,cpp}     # streaming rate conversion for live audio
│   ├── settings.{h,cpp}     # isobar.ini — original's structure, clearer
│   │                        #   key names (see docs/01 sec. 6)
│   └── fft.{h,cpp}          # 4096-pt radix-2 FFT for the scope
├── cli/
│   └── isobar-decode.cpp     # WAV in → .syn/PGM out (builds as isobar-decode)
├── gui/ (FLTK)              # TForm1 + Form2–Form10 replicas (see docs/05)
│   ├── main.cpp             # TForm1: buttons, speed buttons, combos, preview
│   ├── faxview / scopeview  # Image1 preview + Image3 spectrum/waterfall
│   ├── *dialog.cpp          # Form2/4/5/6/8/9/10 replicas
│   └── audio.cpp            # RtAudio live input (22050 Hz mono callback)
└── tests via ctest          # 23 headless tests (cli/*-test.cpp)
```

Threading: dedicated audio callback → lock-free ring buffer → **decoder thread**
→ GUI updates via queued signals. Deliberately does **not** replicate the
original's `TThread::Synchronize`-onto-GUI-thread defect.

## Milestones

### M0 — Foundations (both options share this; ~1–2 weeks)
1. Extract exact constants (FIR taps, twiddles) — re-derived from the WEFAX
   standard specs (see analysis doc §9), not copied from the original binary.
2. Transcribe `sub_402A18` into `core/decoder.cpp` with the exact math:
   51-tap BPF → Hilbert + atan2 → phase diff (2π unwrap) → −0.42744 rad
   reference → ×1102.25 → clamp → 51-tap LPF → 300/450 Hz resonators →
   2205→800 decimation → sync detect (3980–4020 window, width 100–400) →
   fallback tracker with LReSycn/RReSycn hysteresis → line rotate.
3. CLI tool: WAV (16-bit, any channel count, any rate ≥ 6000 Hz —
   resampled to 22050 internally) → `.syn` or PGM.
4. **Validation harness**: collect test signals —
   - record JMH (3.6206 / 7.7931 / 13.9866 MHz USB) off-air, and/or
   - run original KG-FAX under Wine/CrossOver on the same WAV to produce
     golden `.syn` files.
   - Acceptance: replica's `.syn` byte-identical (or within documented
     tolerance if FP ordering differs) to the Wine golden output.

### M1 — Qt GUI skeleton (~1–2 weeks)
- MainWindow replicating TForm1 layout; PreviewWidget + ScopeWidget as custom
  widgets with direct pixel drawing; stub dialogs.
- Audio device enumeration + capture via RtAudio (default device, 22050/16/mono;
  upsample/reject unsupported rates — original assumed the device accepts
  22.05 kHz).

### M2 — Feature parity (~2 weeks)
- Live preview (every-3rd-line, 3×8 averaging, Line# HUD, cursor).
- Zoom ×1/×2/×3 with drag-select; pan; overview restore.
- Rotate 90° / 180°; palettes + negative; spectrum/waterfall toggle.
- `.syn` load/save, BMP/JPG/PNG export, auto-save (`YYYYMMDDHHMM.syn`),
  auto-record arm, 300/450 Hz auto start/stop.
- Form2–Form10 replicas (progress, sync settings editing the same fields,
  palette dialog, device select, file list, about).
- Printing via `QPrinter`.

### M3 — Platform packaging & QA (~1–2 weeks)
- macOS: `.app` bundle, codesign + notarize, universal2.
- Linux: AppImage (or distro packages); BSD: ports/pkg notes.
- HiDPI, font-metric, and theme checks against the reference layout.
- Long-run soak test (19-min full buffer = 2280 lines).

## Effort estimate (solo, part-time)

| Phase | Estimate |
|---|---|
| M0 DSP core + validation | 1–2 weeks |
| M1 GUI skeleton | 1–2 weeks |
| M2 Feature parity | 2–3 weeks |
| M3 Packaging ×3 OSes | 1–2 weeks |
| **Total** | **~6–10 weeks** |

## Risks

- **FIR tap extraction** is the only hard blocker for bit-exactness; everything
  else is in the decompile. Mitigation: get the original `kgfax.exe` (freeware,
  still downloadable from ham archives) and dump `.rdata`.
- **Golden-reference drift**: the original under Wine may behave slightly
  differently on borderline signals; define tolerance up front (e.g., ±1 LSB
  per pixel, identical line geometry).
- Qt HiDPI scaling can shift the window-for-window layout on Retina displays;
  pin widget sizes, test at 1× and 2×.
