/* settings.h - persistent settings, kgfax.ini-compatible schema.
 *
 * Maps docs/01-program-analysis.md section 6: same section and key
 * names (including the original's "LReSycn"/"RReSycn" typos) and plain
 * integer encodings. Storage matches the original: kgfax.ini next to
 * the executable (docs/01 sec. 6: <exe-dir>\kgfax.ini).
 *
 * Which settings actually affect decoding today:
 *   syncwidth -> sync pulse width limits in scan_lines
 *   rresycn   -> max coast lines before re-acquiring sync
 *   sync2thre/lresycn/synthre -> fallback search + lock hysteresis
 *   dettime   -> start/stop-tone detection hysteresis (in 100 ms blocks)
 */
#ifndef ISOBAR_SETTINGS_H
#define ISOBAR_SETTINGS_H

#include <ostream>
#include <string>

struct KgSettings {
    /* [Sync] sync detection/tracking (Form4 "Details" dialog) */
    int sync2thre;    /* Sync2Thre: fallback-search validation level */
    int lresycn;      /* LReSycn:  lock hysteresis count            */
    int rresycn;      /* RReSycn:  release hysteresis -> max coast  */
    int synthre;      /* SyncThre: detection level = 20*i+10        */
    int syncwidth;    /* SyncWidth: n where width = 10*n ms         */
    /* [Det] */
    int dettime;      /* DetTime: start/stop-tone integration time  */
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

/* Built-in defaults. Where the original's defaults are unknown we
 * picked values that keep the M0 decode behavior unchanged
 * (syncwidth=5 -> 100..400-sample pulses, rresycn=60 coasts). */
void settings_defaults(KgSettings &s);

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

/* "<exe-dir>/kgfax.ini", like the original (docs/01 sec. 6). */
std::string settings_path();

/* Read the ini at path into s. Keys/sections not recognized are
 * ignored; fields missing from the file keep their current values.
 * Returns false if the file does not exist or cannot be opened. */
bool settings_read(const std::string &path, KgSettings &s);

/* Write the full ini (schema order as in docs/01 sec. 6).
 * Throws std::runtime_error on I/O failure. */
void settings_write(const std::string &path, const KgSettings &s);

/* Same, to a stream (used by the --dump-settings dev flag). */
void settings_write_stream(std::ostream &os, const KgSettings &s);

#endif
