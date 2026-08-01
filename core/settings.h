/* settings.h - persistent settings, stored as isobar.ini next to the
 * executable (see settings_path()).
 *
 * The file keeps the original's structure - same sections, same plain
 * integer encodings, [Set]/[Wave]/[Form]/[Dir] keys unchanged - but the
 * six tuning keys are RENAMED. The original's names are opaque, two are
 * typos ("Sycn"), and two actively mislead: "SyncWidth" is not a width,
 * and LReSycn/RReSycn give no hint which one locks. Every one of those
 * traps was walked into during the 2026-08-01 audit, so isobar.ini says
 * what it means:
 *
 *   isobar.ini        kgfax.ini     meaning
 *   DarkThreshold     Sync2Thre     brightness at/below which = "dark"
 *   FallbackDepth     SyncThre      min dip below local mean, fallback
 *   ReleaseAfter      LReSycn       invalid lines -> drop lock
 *   LockAfter         RReSycn       valid lines   -> declare lock
 *   MaxJump           SyncWidth     samples the sync may move per line
 *   ToneBlocks        DetTime       consecutive 100 ms blocks of tone
 *
 * The original's names live on in settings_read_kgfax() and in
 * docs/01-program-analysis.md section 6, which documents KG-FAX itself
 * and keeps its vocabulary.
 */
#ifndef ISOBAR_SETTINGS_H
#define ISOBAR_SETTINGS_H

#include "syncscan.h"

#include <ostream>
#include <string>

struct KgSettings {
    /* [Sync] sync detection/tracking (Form4 "Details" dialog) */
    int dark_threshold;  /* brightness at/below this = "dark"        */
    int release_after;   /* invalid lines before the lock is dropped */
    int lock_after;      /* valid lines before the lock is declared  */
    int fallback_depth;  /* fallback validation level = 20*i+10      */
    int max_jump;        /* samples the sync position may move/line  */
    /* [Det] */
    int tone_blocks;     /* tone integration, count of 100 ms blocks */
    /* [Dir] */
    std::string dirname;  /* DirName: auto-save directory */
    /* [Set] */
    int rpm;          /* main form drum-speed combo index  */
    int syn;          /* main form sync-window combo index */
    bool cycleget;    /* CycleGet: auto cyclic save        */
    /* [Wave] */
    int wavedev;      /* WaveDev: wave input device, 0 = default */
    /* [Form] */
    int formx, formy; /* main window position */
};

/* Built-in defaults: the original program's own, read out of the
 * ReadInteger literals in its ini loader (docs/01 sec. 6). The two
 * exceptions are dark_threshold and fallback_depth - see settings.cpp. */
void settings_defaults(KgSettings &s);

/* settings -> decoder thresholds (docs/01 sec. 5-6):
 *   max_jump       -> search_win  (NOT a pulse width; the 100..400-sample
 *                     pulse window is hard-coded and has no ini key)
 *   release_after  -> max_coast   dark_threshold -> dark_th
 *   lock_after     -> lock_hyst   fallback_depth -> fb_thresh
 *   syn combo      -> fallback_win = 40*(i+1) samples, halved on the
 *                     4000 S/s 60-rpm stream
 * Lives here rather than in the GUI so the tests exercise the same
 * mapping the application uses. */
SyncParams sync_params_from_settings(const KgSettings &s);

/* Directory of the running executable (macOS: _NSGetExecutablePath;
 * Windows: GetModuleFileNameW; Linux: /proc/self/exe; "." as fallback).
 * Used for the ini path and as the auto-save fallback directory,
 * matching the original, which keeps everything next to the exe. */
std::string exe_dir();

/* Directory for bundled resources (icons, etc.). Same as exe_dir() for
 * the flat layout (Linux/Windows, dev builds); inside a macOS .app the
 * resources live in Contents/Resources/ — one level up from MacOS/.
 * Returns the first existing candidate, or exe_dir() as a fallback. */
std::string resource_dir();

/* "<exe-dir>/isobar.ini" - our own settings file. It uses the original's
 * schema, but NOT its filename: two keys have diverged in meaning
 * (DEVIATIONS.md #16), and sharing one file would let each program
 * silently mis-tune the other, since both parse it without complaint. */
std::string settings_path();

/* "<exe-dir>/kgfax.ini" - the original program's file. Read once by
 * settings_load() to migrate; never written. */
std::string legacy_settings_path();

enum SettingsSource {
    SETTINGS_DEFAULTS,   /* no file found        */
    SETTINGS_OWN,        /* isobar.ini           */
    SETTINGS_IMPORTED    /* migrated kgfax.ini   */
};

/* Defaults, then isobar.ini if present; failing that a one-time import of
 * a kgfax.ini. Only the unambiguous preferences come across - DirName,
 * rpm, syn, CycleGet, WaveDev, FormX/Y. The [Sync] and [Det] tuning keys
 * are deliberately NOT imported and keep our defaults; see settings.cpp
 * for why (two have diverged in meaning, and such a file may have been
 * written by an older build of this program under the old, wrong units). */
SettingsSource settings_load(KgSettings &s);

/* Same, with explicit paths (the tests use this; settings_load() is this
 * with settings_path() / legacy_settings_path()). */
SettingsSource settings_load_from(const std::string &own_path,
                                  const std::string &legacy_path,
                                  KgSettings &s);

/* Read an isobar.ini at path into s. Keys/sections not recognized are
 * ignored; fields missing from the file keep their current values.
 * Returns false if the file does not exist or cannot be opened. */
bool settings_read(const std::string &path, KgSettings &s);

/* Same, for a file using the ORIGINAL program's key names (docs/01
 * sec. 6). Used by settings_load()'s one-time import. */
bool settings_read_kgfax(const std::string &path, KgSettings &s);

/* Write the full ini (schema order as in docs/01 sec. 6).
 * Throws std::runtime_error on I/O failure. */
void settings_write(const std::string &path, const KgSettings &s);

/* Same, to a stream (used by the --dump-settings dev flag). */
void settings_write_stream(std::ostream &os, const KgSettings &s);

#endif
