/* settings.cpp - see settings.h. Tiny fixed-schema ini reader/writer;
 * not a general ini library (the schema is 14 keys in 6 sections). */

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
    s.sync2thre  = 100;
    s.lresycn    = 5;
    s.rresycn    = 60;   /* = scan_lines MAX_COAST: no behavior change */
    s.synthre    = 10;   /* = 20*0+10, combo index 0 */
    s.syncwidth  = 5;    /* 10*5 ms = 50 ms = 400 samples max pulse  */
    s.dettime    = 100;
    s.dirname    = "";
    s.rpm        = 0;    /* 120 rpm */
    s.syn        = 3;    /* 20 msec, as in the original screenshot   */
    s.cycleget   = false;
    s.wavedev    = 0;
    s.formx      = 100;
    s.formy      = 100;
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
    return exe_dir() + "/kgfax.ini";
}

/* ---- writing ---- */

void settings_write_stream(std::ostream &os, const KgSettings &s)
{
    os << "[Dir]\n"
       << "DirName=" << s.dirname << "\n"
       << "[Sync]\n"
       << "Sync2Thre=" << s.sync2thre << "\n"
       << "LReSycn="  << s.lresycn  << "\n"   /* sic: original typo */
       << "RReSycn="  << s.rresycn  << "\n"   /* sic: original typo */
       << "SyncThre=" << s.synthre   << "\n"
       << "SyncWidth=" << s.syncwidth << "\n"
       << "[Det]\n"
       << "DetTime="  << s.dettime  << "\n"
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

bool settings_read(const std::string &path, KgSettings &s)
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
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (section == "Dir" && key == "DirName")
            s.dirname = val;
        else if (section == "Sync" && key == "Sync2Thre")
            s.sync2thre = to_int(val, s.sync2thre);
        else if (section == "Sync" && key == "LReSycn")
            s.lresycn = to_int(val, s.lresycn);
        else if (section == "Sync" && key == "RReSycn")
            s.rresycn = to_int(val, s.rresycn);
        else if (section == "Sync" && key == "SyncThre")
            s.synthre = to_int(val, s.synthre);
        else if (section == "Sync" && key == "SyncWidth")
            s.syncwidth = to_int(val, s.syncwidth);
        else if (section == "Det" && key == "DetTime")
            s.dettime = to_int(val, s.dettime);
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
        /* unknown keys/sections: ignored on purpose */
    }
    return true;
}
