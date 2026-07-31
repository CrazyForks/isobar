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
    out.sync2thre  = 111;
    out.lresycn    = 22;
    out.rresycn    = 33;
    out.synthre    = 44;
    out.syncwidth  = 7;
    out.dettime    = 555;
    out.dirname    = tmpdir + "/fax dir";
    out.rpm        = 1;
    out.syn        = 2;
    out.cycleget   = true;
    out.wavedev    = 3;
    out.formx      = 640;
    out.formy      = 480;

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

    if (in.sync2thre  != out.sync2thre)  return fail("sync2thre");
    if (in.lresycn    != out.lresycn)    return fail("lresycn");
    if (in.rresycn    != out.rresycn)    return fail("rresycn");
    if (in.synthre    != out.synthre)    return fail("synthre");
    if (in.syncwidth  != out.syncwidth)  return fail("syncwidth");
    if (in.dettime    != out.dettime)    return fail("dettime");
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

    printf("settings-test: PASS\n");
    return 0;
}
