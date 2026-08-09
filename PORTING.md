# Isobar — HF Weather-Fax (WEFAX) Decoder

Isobar is a cross-platform WEFAX (emission J3C) weather-fax decoder that
reimplements the behavior of, and interoperates with, KG-FAX v1.1.3
(K.G, 2009) — including its `.syn` file format, its settings (`.ini`) schema,
and its UI workflow. Settings live in Isobar's own `isobar.ini` rather than
the original's file, and an existing `kgfax.ini` is imported once; see
`DEVIATIONS.md` #6 and #16 for why the two were separated. It is an
independent reimplementation for interoperability; it is NOT affiliated with
or endorsed by the KG-FAX author. See `NOTICE` for the full provenance
statement.

**Start here: [`docs/README.md`](docs/README.md)** — index of the functional
specification and the implementation plans.

Target platforms: macOS / Linux / Windows, Intel and ARM alike. Built with
FLTK (GUI), RtAudio (live audio) and zlib (PNG compression). Licensed under
GPLv3+ (see `LICENSE`).
Prebuilt self-contained packages — two macOS `.dmg`s (Apple Silicon, Intel),
a Windows `.zip`, and x86_64 + aarch64 AppImages — are attached to each
[Release](https://github.com/skgsara/isobar/releases); see the README's
Download section for which file goes with which machine.
