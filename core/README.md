# core/ — Isobar DSP core

Framework-agnostic C++17 WEFAX decoder core, written fresh from the
functional specification in `../docs/01-program-analysis.md` (sections 3 and 4).
No code or tap tables were taken from the original binary; all FIR taps are
re-derived with textbook windowed-sinc design (`filters.cpp`).

- `filters.*` — Hann-windowed-sinc FIR design (lowpass/bandpass/Hilbert)
  + streaming convolution.
- `decoder.*` — FM demod chain at 22050 Hz: 51-tap BPF 1500–2300 Hz →
  quadrature demod (51-tap Hilbert → Q, 25-sample delay → I, atan2 phase
  difference, 2π unwrap, −2π·1500/22050 reference, ×1102.25, clamp 0–255)
  → 51-tap LPF (1200 Hz) → rational 160/441 decimation to 8000 S/s video
  bytes. `video_halve_rate` (pair average) converts to the 60 rpm
  4000 S/s stream (spec 3.2(6)), so syncscan runs unchanged for both
  speeds; CLI `--60`, GUI rpm combo.
- `wavfile.*` — RIFF/WAVE reader: 16-bit PCM, any channels (mono
  downmix), **any sample rate from 6000 Hz up**. 22050 Hz is used as-is,
  44100 Hz is decimated 2:1 with a 63-tap anti-alias FIR, and every other
  rate (SDR recorders commonly write 8000/12000/48000) goes through
  `resample.*` in either direction. Below 6000 Hz there is no Nyquist room
  for the 2300 Hz white subcarrier, so it is rejected with a clear error,
  as are non-PCM and non-16-bit files. PCM tagged
  **`WAVE_FORMAT_EXTENSIBLE`** (`0xFFFE`) is read as well: the effective
  format tag comes from the SubFormat GUID, whose fixed
  `KSDATAFORMAT_SUBTYPE` tail is checked so a genuinely different format
  still fails. macOS `afconvert -f WAVE` writes that header for any source
  with a channel layout, which is every `.m4a` (`wavrate-test`).
- `syncscan.*` — line extraction (120/60 rpm): lines are emitted on a
  fixed 4000-sample grid from stream start (preamble included, like the
  original), rotated by the tracked phase offset (sync edge → index 0);
  the grid never moves, tracking only adjusts the phase. Sync pulses =
  dark runs with width 100–400 samples, period 3980–4020 vs. the
  previous line's edge. When the shape check fails, two fallbacks in
  order: first `fallback_edge()`, the original's own tracker ported
  (boxcar minimum-mean over the binarised video, gated on a bright→dark
  edge, validated against the absolute `fb_mean` bound = the original's
  `SyncThre` 30, plus its `MaxJump` guard); then, only if that declines,
  our older min-brightness search validated by a dip depth below the
  local mean. The ported one is the more selective of the two but fires
  less often, and the pair together beat either alone on every fixture.
  Two rules keep the sync strip solid, both in `syncscan.h` and shared
  with `live.cpp` so the batch and live paths cannot drift apart. The
  strip is rotated to index 0 — the seam where a line wraps — so a phase
  error does not shift a line, it **splits** the strip across both ends,
  which is what makes such lines unreadable (`slew-test`).
  `sync_step_lock()` handles a **genuine step** in the sync position:
  real transmissions jump further than any narrow search reaches — the
  off-air recording steps −162 samples ten times mid-picture (a networked
  SDR's clock-slip correction), and `jmh sample.wav` does it at line 930
  where a new transmission starts. It takes the darkest `syn` window in
  the whole line, but only when that pulse is unambiguous — nearest rival
  at least 300 samples away and `DarkThreshold`/2 brighter, and the dark
  run it sits in is a plausible sync-pulse width (the same 100..400
  samples `find_sync_edges` requires, which is what keeps a chart's wide
  black margin out) — and only when the **next** line agrees with it. That one line of **lookahead** is
  what makes a whole-line search safe here, where a bare one re-locks onto
  picture content (`phasing-test`), and confirming forwards rather than
  backwards is what lets the step be followed on the very line it starts
  on — the line whose strip would otherwise be split. `sync_slew()` handles everything
  else: a move within `MaxJump` is taken whole, anything further is
  rate-limited to `MaxJump`/4 samples, doubling for each consecutive line
  agreeing on the direction. The original rejects a far fallback result
  outright and we cannot, because that loses the real steps
  (`DEVIATIONS.md` #16).
  `ReleaseAfter` invalid lines → drop the lock, `LockAfter` valid lines →
  declare it, re-acquiring via a junk-tolerant period chain
  (spec 3.2(7)(8)(10)). NOTE `LockAfter` is what limits tolerance of a
  *gappy* source: the chain needs `LockAfter` consecutive valid periods,
  so an audio dropout more often than every ~3 s stops the decoder locking
  at all. Lowering it to 1–3 fixes that and costs nothing on clean audio —
  measured, `ROADMAP.md` M6. 4000 samples → 1500 px via `line[8*i/3]`.
  Re-acquisition after a release is confined to the neighbourhood of the
  phase already held — a transmission's sync position does not move, only
  its quality does — and widens back to the whole line only after
  3 × `ReleaseAfter` lines with no lock at all, so a genuine change of
  transmission can still be followed. Without that confinement an
  all-dark phasing preamble releases the lock and the full-line search
  re-locks onto a dark feature in the picture (`phasing-test`).
  NOTE the original's names for those two counters are the other way
  round from how they read: `LReSycn` releases, `RReSycn` locks. The port
  had them swapped until v1.2.0.
- `fft.*` — 4096-point radix-2 FFT + Hann window, magnitudes in dBFS;
  used by the GUI spectrum scope (spec section 3.3).
- `settings.*` — settings in the original's structure (spec section 6),
  stored next to the executable like the original, as `exe_dir()` +
  `/isobar.ini`. The six tuning keys are renamed to say what they mean
  (`DarkThreshold`, `FallbackDepth`, `ReleaseAfter`, `LockAfter`,
  `MaxJump`, `ToneBlocks`); `settings_read_kgfax()` still parses the
  original's names, and `docs/01` §6 has the mapping table.
  Eleven of the twelve defaults are the original's own; `DarkThreshold`
  is ours because our sync detector computes that quantity differently
  (`DEVIATIONS.md` #16) — which is also why the file is no longer shared
  with the original. `settings_load()` imports an
  existing `kgfax.ini` once, taking only the unambiguous preferences.
  `sync_params_from_settings()` is the single settings→decoder mapping,
  used by both the GUI and the tests.
  `scan_lines` takes an optional
  `SyncParams`; `sync_params_from_settings()` in `settings.*` is the one
  place the ini maps onto it (`MaxJump` → `search_win`, `ReleaseAfter` →
  `max_coast`, `LockAfter` → `lock_hyst`, …). The 100..400-sample pulse
  window is NOT settable — it is hard-coded in the original and has no
  ini key.
- `tonedetect.*` — 300/450 Hz start/stop tone resonators (spec 3.2(5)):
  sign(video) → ±10000 into 2-pole resonators (10 Hz BW), envelope LPF,
  100 ms block max. Runs on the 8000 S/s video stream (the original
  runs it at 22050 pre-decimation; tones survive decimation, so
  coefficients were re-derived at 8000 — see file header). Threshold
  3e9 calibrated on the sample's real tones (~1.3e10) vs. test-chart
  wedges (~1.5e9).
- `synfile.*` — `.syn` read/write (SynFax2 + legacy "Syn Fax" magic),
  byte-exact round-trip incl. the radix-255 line-count encoding fixed to
  match the original 2026-07-31 (interop confirmed against the real
  KG-FAX). Used by the CLI, the GUI Save, and auto-save.
- `bmpfile.*` — BMP writer for the Form6 image-export path (and the
  auto-save `.bmp` format option); matches the original's BMP layout.
- `palette.*` — the 4 palette modes (monotone / an unnamed blue→red
  ramp, not selectable in the UI / "blue ray" / "color temp") + invert,
  applied at render time (Form5 color processing).
- `live.*` — streaming line assembly: same algorithms as `syncscan.*`
  restructured for incremental feeds (edge decisions lag by up to
  max_pulse; "no edge" is decidable only past expected+search_win+
  max_pulse). Byte-identical output to the batch scanner
  (`ctest --test-dir build -R live-test`). `nudge_phase()` is the manual
  sync align (docs/01 §3.2): the UI thread posts a phase shift in
  samples, `pump()` applies it at the next line boundary and keeps the
  lock so the search stays centred on the position the user gave
  (`ctest --test-dir build -R manual-sync-test`). A hand-placed position
  also suppresses `sync_step_lock` until the decoder earns a shape lock of
  its own: a whole-line search would walk straight off to the strongest
  sync-looking pulse, which is the very thing the manual position exists
  to override. `finish()` releases the
  line `sync_step_lock`'s lookahead is holding back; call it on the feed
  thread when the stream ends, or the last line never comes out.
  `request_finish()`/`finish_done()` are the same thing for a UI thread
  that cannot call it directly — the request is posted and the next
  `feed()` performs it on the audio thread, which is how the GUI's
  Scan-off keeps a reception's last line. Both paths are checked against
  the batch decode (`live-test`).
- `resample.*` — streaming anti-alias resampler (any rate → 22050 Hz,
  and up, for live capture and the `playwav` dev tool).

GUI taps (optional callback args, CLI behavior unchanged):
`fm_decode` can hand out each 4096-sample BPF block (scope input) and
the 100 ms tone levels (Control LED / Auto ctl); `scan_lines` can
report the per-line state (status LEDs). Live reception uses
`LiveScan` + `Resampler` + `ToneDetect` directly (gui/audio.cpp,
gui/main.cpp `live_sample`).

## Known simplifications / future work

- The fallback edge validation has two tiers (see the `syncscan.*` entry
  above): the ported original first (`fallback_edge()`, since v1.3.0),
  then our older dip-depth search as the second chance. `DEVIATIONS.md`
  #16 records why the original's thresholds were not adopted wholesale.
- Decimation 22050→8000 uses linear interpolation; adequate because the
  video LPF (1200 Hz) is far below the 4 kHz output Nyquist.
- 60 rpm mode has no real-world recording test yet (only the synthetic
  stretch test in `cli/rpm60-test.cpp`); the .syn mode byte for 60 rpm
  files is unknown, so .syn output still writes mode 0.
