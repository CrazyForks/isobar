# Option B — macOS-Native Port (Swift + AppKit)

> **Superseded (decision 2026-07-29):** Option A (FLTK + RtAudio) was chosen
> instead. This file is kept as historical reference only; the decision
> rationale is in `04-decision-guide.md`. See `docs/README.md` Status for the
> current implementation direction.

Goal: a window-for-window, bug-for-bug replica of KG-FAX v1.1.3 as a native
**macOS-only** application. Linux/BSD are explicitly out of scope — choosing
this option means accepting a full GUI rewrite if that ever changes.

## Why this option

- Cheapest path to a polished macOS app (~30–40% less total effort than Option A
  *for macOS alone*).
- CoreAudio is a clean, direct replacement for `waveIn*`; no cross-platform
  audio abstraction to debug.
- Native printing (`NSPrintOperation`), native file dialogs, native Retina
  rendering with no HiDPI surprises.

## Why it might not be

- **The replica goal fights the framework.** KG-FAX is a flat 2009 VCL form;
  AppKit wants modern macOS idioms. Achieving window-for-window fidelity means
  custom `NSView`s with manual pixel blitting anyway — you get little of
  AppKit's "free" nativeness, since the original has none.
- **Zero portability.** The GUI is ~40% of total effort; a later Linux request
  re-spends all of it. (The DSP core in C++ remains reusable — keep it
  framework-free either way.)
- Swift/AppKit pixel work (constant scanline writes, waterfall scroll) is more
  awkward than Qt's `QImage`/`scanLine()` model.

## Architecture

```
kgfax-macos/
├── KGFaxCore/               # same framework-agnostic C++ core as Option A
│   ├── decoder.{h,cpp}      # sub_402A18 transcription (see Option A M0)
│   ├── tables / image_buffer / palette / synfile / settings / fft
│   └── module.modulemap     # expose to Swift via Clang importer
├── KGFaxCLI/                # WAV → .syn/PNG validation harness (shared w/ A)
└── KGFaxApp/ (Swift, AppKit — use AppKit, not SwiftUI, for control fidelity)
    ├── MainWindowController       # TForm1 replica (XIB or code layout,
    │                              #   pinned control sizes/positions)
    ├── PreviewView (NSView)       # Image1 replica: 500-px live preview,
    │                              #   drag-zoom via custom tracking rect
    ├── ScopeView (NSView)         # Image3 replica: spectrum + waterfall,
    │                              #   backed by a CVPixelBuffer/NSBitmapImageRep
    ├── AudioEngine                # AVAudioEngine input tap @ native rate,
    │                              #   resample to 22050 mono int16 → core
    ├── Dialogs/                   # Form2–Form10 replicas as NSPanel/
    │                              #   NSWindowController subclasses
    └── PrintSupport               # NSPrintOperation rendering the 1500×2280
                                   #   buffer scaled to page (StretchDIBits equiv)
```

Pixel rendering: keep a `NSBitmapImageRep` (24-bit RGB) per view, write
scanlines directly into its `bitmapData`, `setNeedsDisplay` per new line — the
AppKit equivalent of the original's direct bitmap plotting.

Threading: `AVAudioEngine` tap → ring buffer → decoder `DispatchQueue` →
`DispatchQueue.main` for view updates. Same deliberate deviation as Option A:
do **not** replicate the GUI-thread decoding defect.

## Milestones

### M0 — Shared DSP core (identical to Option A M0; ~1–2 weeks)
Constants extraction, `sub_402A18` transcription, CLI WAV→`.syn`, and the
Wine-golden validation harness. This milestone is framework-agnostic — **do it
before committing to B**, and keep it in C++ so it survives a later switch to A.

### M1 — AppKit shell (~1–2 weeks)
- MainWindow replicating TForm1: 11 buttons, 3 toggle buttons (SpeedButtons),
  2 combo boxes (`NSPopUpButton`), preview + scope views, LED indicators
  (`NSLevelIndicator` or custom colored views to mimic the TProgressBar LEDs).
- AudioEngine capture with sample-rate conversion to 22050/16/mono.
- Device selection dialog (Form9 replica) using `AVAudioDevice` enumeration;
  the original's `SNDVOL32` buttons map to opening System Settings → Sound.

### M2 — Feature parity (~1–2 weeks)
Same checklist as Option A M2: live preview, ×1/×2/×3 drag zoom + pan, 90°/180°
rotate, palettes + negative, scope toggle, `.syn` I/O + image export, auto-save
(`YYYYMMDDHHMM.syn`), 300/450 Hz auto start/stop, Form2–Form10 replicas,
`NSPrintOperation` printing.

### M3 — macOS polish & release (~1 week)
- `.app` bundle, universal2, codesign + notarize, Sparkle or manual updates.
- Retina 1×/2× layout checks; long-run soak test.

## Effort estimate (solo, part-time)

| Phase | Estimate |
|---|---|
| M0 DSP core + validation | 1–2 weeks |
| M1 AppKit shell + audio | 1–2 weeks |
| M2 Feature parity | 1–2 weeks |
| M3 macOS release | ~1 week |
| **Total (macOS only)** | **~4–7 weeks** |

## Risks

- Same M0 risks as Option A (FIR tap extraction, golden-reference tolerance).
- **Scope creep toward "native feel"**: once in AppKit there will be constant
  temptation to modernize the UI (toolbars, dark mode, SF Symbols). Each one is
  a deviation from window-for-window. Decide up front which deviations are
  acceptable and record them in a DEVIATIONS.md.
- If Linux/BSD ever enters scope, the exit path is: keep `KGFaxCore` + CLI,
  discard `KGFaxApp`, build Option A's Qt GUI on the same core (i.e., pay M1–M2
  again in Qt).
