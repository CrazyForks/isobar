/* settings.cpp - see settings.h. Tiny fixed-schema ini reader/writer;
 * not a general ini library (the schema is 13 keys in 6 sections). */

#include "settings.h"

#include <climits>
#include <fstream>
#include <sstream>
#include <stdexcept>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>          /* readlink on /proc/self/exe (Linux) */
#endif

#if defined(_WIN32)
#include <sys/stat.h>        /* _wstat (directory existence check)   */
#include <sys/types.h>
#else
#include <sys/stat.h>        /* stat (directory existence check)     */
#endif

void settings_defaults(KgSettings &s)
{
    /* These are the ORIGINAL program's defaults, taken from the
     * TIniFile::ReadInteger fallback arguments in its ini loader
     * (docs/01-program-analysis.md sec. 6). Do not "tidy" them.
     *
     * Two exceptions, dark_threshold and fallback_depth: the keys exist in both
     * programs and parse identically, but our detector computes those
     * two quantities differently (moving average vs binarised raw video;
     * dip-depth vs an absolute bound on a boxcar mean), so the
     * original's 20 and 30 do not carry over - they cost 1550 of 1851
     * lines on an off-air recording. See DEVIATIONS.md #16. */
    s.dark_threshold = 96;  /* ours, cf. Sync2Thre 20 */
    s.fallback_depth = 10;  /* ours, cf. SyncThre 30; = 20*0+10, "Strict" */
    s.release_after  = 10;  /* LReSycn:  invalid lines -> drop lock   */
    s.lock_after     = 5;   /* RReSycn:  valid lines -> declare lock  */
    s.max_jump       = 20;  /* SyncWidth: samples of position jump    */
    s.tone_blocks    = 20;  /* DetTime:  100 ms blocks of tone = 2 s  */
    s.dirname    = "";
    s.rpm        = 0;    /* 120 rpm */
    s.syn        = 3;    /* 20 msec, as in the original screenshot   */
    s.cycleget   = true;
    s.wavedev    = 0;
    s.formx      = 0;
    s.formy      = 0;
}

SyncParams sync_params_from_settings(const KgSettings &s)
{
    SyncParams sp = sync_default_params();
    sp.search_win   = s.max_jump;
    sp.max_coast    = s.release_after;
    sp.lock_hyst    = s.lock_after;
    sp.dark_th      = s.dark_threshold;
    sp.fb_thresh    = s.fallback_depth;
    sp.fallback_win = 40 * (s.syn + 1) / (s.rpm == 1 ? 2 : 1);
    return sp;
}

std::string exe_dir()
{
#if defined(_WIN32)
    /* MAX_PATH (260) on Windows; wide API because GetModuleFileName is the
     * safe, always-available path resolver there. */
    wchar_t wbuf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return ".";
    /* Narrow UTF-8 for the rest of the codebase (paths use '/' below, which
     * the Windows file APIs accept alongside '\\'). */
    char buf[MAX_PATH * 3];
    int m = WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)n, buf, sizeof buf, nullptr, nullptr);
    if (m <= 0)
        return ".";
    std::string p(buf, m);
#else
    char buf[PATH_MAX];
#if defined(__APPLE__)
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0)
        return ".";
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return ".";
    buf[n] = 0;
#endif
    std::string p(buf);
#endif
    std::string::size_type slash = p.find_last_of('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

namespace { /* small helpers: does a path exist, and is it a directory? */
bool path_exists(const std::string &path)
{
#if defined(_WIN32)
    struct _stat64i32 st;
    return _stat(path.c_str(), &st) == 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}
bool dir_exists(const std::string &path)
{
#if defined(_WIN32)
    struct _stat64i32 st;
    return _stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}
} /* namespace */

std::string resource_dir()
{
    std::string exe = exe_dir();
    /* Flat layout (Linux/Windows, dev builds): resources sit next to the exe.
     * Check the PNG as a file (not a directory — the old dir_exists() call
     * was always false here, leaving this branch dead). */
    if (path_exists(exe + "/isobar-128.png"))
        return exe;
    /* macOS .app bundle: exe is in Contents/MacOS/, resources in
     * Contents/Resources/ — one level up from the exe dir. */
    std::string::size_type slash = exe.find_last_of('/');
    std::string contents = (slash == std::string::npos) ? "." : exe.substr(0, slash);
    std::string res = contents + "/Resources";
    if (dir_exists(res))
        return res;
    return exe;   /* fallback: let the caller's open fail cleanly */
}

std::string settings_path()
{
    return exe_dir() + "/isobar.ini";
}

std::string legacy_settings_path()
{
    return exe_dir() + "/kgfax.ini";
}

SettingsSource settings_load(KgSettings &s)
{
    return settings_load_from(settings_path(), legacy_settings_path(), s);
}

SettingsSource settings_load_from(const std::string &own_path,
                                  const std::string &legacy_path,
                                  KgSettings &s)
{
    settings_defaults(s);
    if (settings_read(own_path, s))
        return SETTINGS_OWN;

    /* One-time migration from a kgfax.ini. The WHOLE [Sync]/[Det] tuning
     * block is held back and keeps the defaults set above; only the
     * unambiguous preferences below are imported.
     *
     * Two reasons, and the second is the one that bites:
     *  - Sync2Thre and SyncThre exist in both programs and parse fine,
     *    but feed differently-shaped formulas here, so the original's
     *    values mis-tune our detector (DEVIATIONS.md #16).
     *  - A kgfax.ini next to the executable may well have been written by
     *    an OLDER BUILD OF THIS PROGRAM, back when LReSycn/RReSycn were
     *    swapped and SyncWidth/DetTime were on different scales. Such a
     *    file is indistinguishable from the original's, and reading it
     *    under the corrected mapping silently produces nonsense (e.g.
     *    RReSycn=60 -> a 60-line chain needed before sync locks).
     * Decoder tuning is rarely customised and is one dialog away; the
     * folder, device and window position are the settings worth keeping. */
    KgSettings legacy;
    settings_defaults(legacy);
    if (!settings_read_kgfax(legacy_path, legacy))
        return SETTINGS_DEFAULTS;

    s.dirname  = legacy.dirname;
    s.rpm      = legacy.rpm;
    s.syn      = legacy.syn;
    s.cycleget = legacy.cycleget;
    s.wavedev  = legacy.wavedev;
    s.formx    = legacy.formx;
    s.formy    = legacy.formy;
    return SETTINGS_IMPORTED;
}

/* ---- writing ---- */

void settings_write_stream(std::ostream &os, const KgSettings &s)
{
    os << "[Dir]\n"
       << "DirName=" << s.dirname << "\n"
       << "[Sync]\n"
       << "DarkThreshold=" << s.dark_threshold << "\n"
       << "ReleaseAfter="  << s.release_after  << "\n"
       << "LockAfter="     << s.lock_after     << "\n"
       << "FallbackDepth=" << s.fallback_depth << "\n"
       << "MaxJump="       << s.max_jump       << "\n"
       << "[Det]\n"
       << "ToneBlocks=" << s.tone_blocks << "\n"
       << "[Set]\n"
       << "rpm="      << s.rpm      << "\n"
       << "syn="      << s.syn      << "\n"
       << "CycleGet=" << (s.cycleget ? 1 : 0) << "\n"
       << "[Wave]\n"
       << "WaveDev="  << s.wavedev  << "\n"
       << "[Form]\n"
       << "FormX="    << s.formx    << "\n"
       << "FormY="    << s.formy    << "\n";
}

void settings_write(const std::string &path, const KgSettings &s)
{
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc);
    if (!f)
        throw std::runtime_error("cannot write " + path);
    settings_write_stream(f, s);
    if (!f)
        throw std::runtime_error("cannot write " + path);
}

/* ---- reading ---- */

static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static int to_int(const std::string &v, int fallback)
{
    char *end = 0;
    long n = strtol(v.c_str(), &end, 10);
    return (end && *end == '\0') ? (int)n : fallback;
}

/* Walk an ini, handing each section/key/value to `assign`. Both schemas
 * share the file syntax and the [Set]/[Wave]/[Form]/[Dir] keys; they
 * differ only in the [Sync]/[Det] names, which is what `assign` decides. */
static bool parse_ini(const std::string &path, KgSettings &s,
                      void (*assign)(const std::string &section,
                                     const std::string &key,
                                     const std::string &val, KgSettings &s))
{
    std::ifstream f(path.c_str());
    if (!f)
        return false;

    std::string section, line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[') {
            size_t r = line.find(']');
            section = (r == std::string::npos) ? "" : line.substr(1, r - 1);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        assign(section, trim(line.substr(0, eq)),
               trim(line.substr(eq + 1)), s);
        /* unknown keys/sections: ignored on purpose */
    }
    return true;
}

/* Keys shared by both schemas. */
static bool assign_common(const std::string &section, const std::string &key,
                          const std::string &val, KgSettings &s)
{
    if (section == "Dir" && key == "DirName")
        s.dirname = val;
    else if (section == "Set" && key == "rpm")
        s.rpm = to_int(val, s.rpm);
    else if (section == "Set" && key == "syn")
        s.syn = to_int(val, s.syn);
    else if (section == "Set" && key == "CycleGet")
        s.cycleget = to_int(val, s.cycleget) != 0;
    else if (section == "Wave" && key == "WaveDev")
        s.wavedev = to_int(val, s.wavedev);
    else if (section == "Form" && key == "FormX")
        s.formx = to_int(val, s.formx);
    else if (section == "Form" && key == "FormY")
        s.formy = to_int(val, s.formy);
    else
        return false;
    return true;
}

/* isobar.ini: our own names for the tuning block (see settings.h). */
static void assign_isobar(const std::string &section, const std::string &key,
                          const std::string &val, KgSettings &s)
{
    if (assign_common(section, key, val, s))
        return;
    if (section == "Sync" && key == "DarkThreshold")
        s.dark_threshold = to_int(val, s.dark_threshold);
    else if (section == "Sync" && key == "ReleaseAfter")
        s.release_after = to_int(val, s.release_after);
    else if (section == "Sync" && key == "LockAfter")
        s.lock_after = to_int(val, s.lock_after);
    else if (section == "Sync" && key == "FallbackDepth")
        s.fallback_depth = to_int(val, s.fallback_depth);
    else if (section == "Sync" && key == "MaxJump")
        s.max_jump = to_int(val, s.max_jump);
    else if (section == "Det" && key == "ToneBlocks")
        s.tone_blocks = to_int(val, s.tone_blocks);
}

/* kgfax.ini: the ORIGINAL program's names, typos and all. Only used by
 * settings_load()'s import, which discards the tuning block anyway - the
 * fields are still filled so the parser stays a faithful reader of the
 * documented schema (docs/01 sec. 6). */
static void assign_kgfax(const std::string &section, const std::string &key,
                         const std::string &val, KgSettings &s)
{
    if (assign_common(section, key, val, s))
        return;
    if (section == "Sync" && key == "Sync2Thre")
        s.dark_threshold = to_int(val, s.dark_threshold);
    else if (section == "Sync" && key == "LReSycn")     /* sic: original typo */
        s.release_after = to_int(val, s.release_after);
    else if (section == "Sync" && key == "RReSycn")     /* sic: original typo */
        s.lock_after = to_int(val, s.lock_after);
    else if (section == "Sync" && key == "SyncThre")
        s.fallback_depth = to_int(val, s.fallback_depth);
    else if (section == "Sync" && key == "SyncWidth")
        s.max_jump = to_int(val, s.max_jump);
    else if (section == "Det" && key == "DetTime")
        s.tone_blocks = to_int(val, s.tone_blocks);
}

bool settings_read(const std::string &path, KgSettings &s)
{
    return parse_ini(path, s, assign_isobar);
}

bool settings_read_kgfax(const std::string &path, KgSettings &s)
{
    return parse_ini(path, s, assign_kgfax);
}
