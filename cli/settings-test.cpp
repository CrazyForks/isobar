/* settings-test - round-trip check for core/settings.cpp.
 * Registered with ctest; run via `ctest --test-dir build -R settings-test`.
 *
 * Writes a fully non-default KgSettings to a temp ini, reads it back,
 * and compares every field. Also prints the ini for eyeballing the
 * schema (docs/01-program-analysis.md sec. 6).
 */
#include "../core/settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

static int fail(const char *field)
{
    printf("FAIL: %s\n", field);
    return 1;
}

int main()
{
    /* Use the OS temp dir (portable — "/tmp" doesn't exist on Windows). */
    namespace fs = std::filesystem;
    std::string tmpdir = fs::temp_directory_path().string();
    std::string path = tmpdir + "/isobar-settings-test.ini";

    KgSettings out;
    settings_defaults(out);
    out.dark_threshold = 111;
    out.release_after  = 22;
    out.lock_after     = 33;
    out.fallback_depth = 44;
    out.max_jump       = 7;
    out.tone_blocks    = 555;
    out.dirname        = tmpdir + "/fax dir";
    out.rpm            = 1;
    out.syn            = 2;
    out.cycleget       = true;
    out.wavedev        = 3;
    out.formx          = 640;
    out.formy          = 480;

    try {
        settings_write(path, out);
    } catch (const std::exception &e) {
        printf("FAIL: write: %s\n", e.what());
        return 1;
    }

    /* show the written ini for the record */
    printf("---- %s ----\n", path.c_str());
    settings_write_stream(std::cout, out);
    printf("--------------\n");

    KgSettings in;
    settings_defaults(in);          /* prove every key is read back */
    if (!settings_read(path.c_str(), in))
        return fail("settings_read returned false");

    if (in.dark_threshold != out.dark_threshold) return fail("dark_threshold");
    if (in.release_after  != out.release_after)  return fail("release_after");
    if (in.lock_after     != out.lock_after)     return fail("lock_after");
    if (in.fallback_depth != out.fallback_depth) return fail("fallback_depth");
    if (in.max_jump       != out.max_jump)       return fail("max_jump");
    if (in.tone_blocks    != out.tone_blocks)    return fail("tone_blocks");
    if (in.dirname    != out.dirname)    return fail("dirname");
    if (in.rpm        != out.rpm)        return fail("rpm");
    if (in.syn        != out.syn)        return fail("syn");
    if (in.cycleget   != out.cycleget)   return fail("cycleget");
    if (in.wavedev    != out.wavedev)    return fail("wavedev");
    if (in.formx      != out.formx)      return fail("formx");
    if (in.formy      != out.formy)      return fail("formy");

    /* missing file must report false, not crash */
    KgSettings d;
    settings_defaults(d);
    std::string missing = tmpdir + "/definitely-not-here-kgfax.ini";
    if (settings_read(missing.c_str(), d))
        return fail("missing file reported as read");

    /* An out-of-range integer must fall back, not WRAP into a plausible
     * one. `(int)strtol(...)` turned 5000000000 into 705032704 - which
     * then gets clamped downstream and looks like a deliberate setting,
     * so a typo'd or corrupted ini mis-tuned the decoder in silence
     * (audit 2026-08-09). Written as a raw ini so the value never passes
     * through settings_write's own formatting. */
    {
        std::string bad = tmpdir + "/isobar-settings-range.ini";
        {
            std::ofstream f(bad.c_str());
            f << "[Sync]\n"
              << "MaxJump=5000000000\n"          /* > INT_MAX */
              << "ReleaseAfter=-99999999999\n"   /* < INT_MIN */
              << "LockAfter=7\n";                /* in range: must be kept */
        }
        KgSettings r;
        settings_defaults(r);
        if (!settings_read(bad.c_str(), r))
            return fail("range ini not read");
        if (r.max_jump != 20)
            return fail("out-of-range MaxJump did not fall back to default");
        if (r.release_after != 10)
            return fail("out-of-range ReleaseAfter did not fall back");
        if (r.lock_after != 7)
            return fail("in-range LockAfter was not kept");
        fs::remove(bad);
    }

    /* The defaults are the ORIGINAL program's, lifted from the
     * TIniFile::ReadInteger literals in its ini loader (docs/01 sec. 6).
     * They are not ours to tidy, so pin every one of them.
     * dark_threshold is the one deliberate exception: the key exists in
     * both programs but feeds a differently-shaped formula here, so the
     * original's 20 would mis-tune our detector (DEVIATIONS.md #16).
     * fallback_depth used to be the second exception while it drove our
     * dip-depth second chance; since it points at fb_mean (the original's
     * own quantity) the original's 30 is again the right default. */
    KgSettings def;
    settings_defaults(def);
    if (def.release_after != 10) return fail("default ReleaseAfter != 10");
    if (def.lock_after    != 5)  return fail("default LockAfter != 5");
    if (def.max_jump      != 20) return fail("default MaxJump != 20");
    if (def.tone_blocks   != 20) return fail("default ToneBlocks != 20");
    if (def.rpm       != 0)   return fail("default rpm != 0");
    if (def.syn       != 3)   return fail("default syn != 3");
    if (!def.cycleget)        return fail("default CycleGet != 1");
    if (def.wavedev   != 0)   return fail("default WaveDev != 0");
    if (def.formx     != 0)   return fail("default FormX != 0");
    if (def.formy     != 0)   return fail("default FormY != 0");
    if (!def.dirname.empty()) return fail("default DirName not empty");
    if (def.dark_threshold != 96) return fail("default DarkThreshold != 96 (ours)");
    if (def.fallback_depth != 30) return fail("default FallbackDepth != 30 (SyncThre)");
    /* FallbackDepth must stay a legal Form4 combo value: 20*index + 10 */
    if ((def.fallback_depth - 10) % 20 != 0)
        return fail("default FallbackDepth is not a 20*i+10 combo value");

    /* The settings -> decoder mapping must not drift back to the swapped
     * reading (LReSycn releases, RReSycn locks), the old pulse-width
     * misreading of SyncWidth, or FallbackDepth feeding our dip-depth
     * second chance instead of the original's fb_mean. */
    SyncParams sp = sync_params_from_settings(def);
    if (sp.max_coast  != def.release_after) return fail("ReleaseAfter -> max_coast");
    if (sp.lock_hyst  != def.lock_after)    return fail("LockAfter -> lock_hyst");
    if (sp.search_win != def.max_jump)      return fail("MaxJump -> search_win");
    if (sp.fb_mean    != def.fallback_depth) return fail("FallbackDepth -> fb_mean");
    if (sp.fb_thresh  != 10)
        return fail("fb_thresh must stay hard-coded at 10 (no ini key)");
    if (sp.min_pulse != 100 || sp.max_pulse != 400)
        return fail("pulse window must stay hard-coded at 100..400");

    /* ---- one-time migration from the original's kgfax.ini ---- */
    {
        std::string own    = tmpdir + "/isobar-migrate-own.ini";
        std::string legacy = tmpdir + "/isobar-migrate-legacy.ini";
        std::remove(own.c_str());
        std::remove(legacy.c_str());

        /* The dangerous shape of legacy file: the [Sync]/[Det] block as an
         * OLDER BUILD OF THIS PROGRAM used to write it, when LReSycn and
         * RReSycn were swapped and SyncWidth/DetTime were on different
         * scales. Every tuning value here differs from our defaults, so a
         * leak is visible; read under today's mapping they would mean a
         * 60-line lock chain and a 10-second tone hold. */
        /* ofstream, not fopen: MSVC flags fopen as C4996 and the build
         * must stay warning-free on all five CI runners. */
        {
            std::ofstream lf(legacy.c_str());
            if (!lf) return fail("cannot write legacy fixture");
            lf << "[Dir]\nDirName=C:\\fax\n[Sync]\nSync2Thre=100\nLReSycn=5\n"
                  "RReSycn=60\nSyncThre=50\nSyncWidth=5\n[Det]\nDetTime=100\n"
                  "[Set]\nrpm=1\nsyn=2\nCycleGet=0\n[Wave]\nWaveDev=4\n"
                  "[Form]\nFormX=300\nFormY=200\n";
        }

        /* no isobar.ini -> import the legacy file */
        KgSettings m;
        if (settings_load_from(own, legacy, m) != SETTINGS_IMPORTED)
            return fail("kgfax.ini present but not imported");
        /* the unambiguous preferences come across */
        if (m.dirname != "C:\\fax") return fail("import: DirName");
        if (m.rpm != 1 || m.syn != 2 || m.cycleget) return fail("import: [Set]");
        if (m.wavedev != 4) return fail("import: WaveDev");
        if (m.formx != 300 || m.formy != 200) return fail("import: FormX/Y");
        /* the whole [Sync]/[Det] tuning block must NOT: two keys have
         * diverged in meaning, and such a file may have been written by an
         * older build of this program under the old, wrong units. The
         * fixture's values are all different from our defaults, so any
         * leak shows up here. */
        if (m.dark_threshold != def.dark_threshold) return fail("import: Sync2Thre leaked");
        if (m.fallback_depth != def.fallback_depth) return fail("import: SyncThre leaked");
        if (m.release_after  != def.release_after)  return fail("import: LReSycn leaked");
        if (m.lock_after     != def.lock_after)     return fail("import: RReSycn leaked");
        if (m.max_jump       != def.max_jump)       return fail("import: SyncWidth leaked");
        if (m.tone_blocks    != def.tone_blocks)    return fail("import: DetTime leaked");

        /* once isobar.ini exists it wins, and the legacy file is ignored */
        settings_write(own, m);
        KgSettings m2;
        if (settings_load_from(own, legacy, m2) != SETTINGS_OWN)
            return fail("isobar.ini present but not preferred");
        if (m2.wavedev != 4) return fail("isobar.ini not actually read");

        /* neither file: plain defaults */
        std::remove(own.c_str());
        std::remove(legacy.c_str());
        KgSettings m3;
        if (settings_load_from(own, legacy, m3) != SETTINGS_DEFAULTS)
            return fail("no files but source is not SETTINGS_DEFAULTS");
        if (m3.max_jump != def.max_jump) return fail("no files: not defaults");
    }

    printf("settings-test: PASS\n");
    return 0;
}
