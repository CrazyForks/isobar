/* audio-restart-test - start/stop the capture device repeatedly.
 *
 * Reproduces the "cannot open audio input: ... setting disconnect
 * listener" error seen when Scan is toggled off and on again.
 * Uses the first available input device (BlackHole if present).
 * Exit 0 if all restarts succeed, 1 otherwise.
 */
#include "../gui/audio.h"

#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>

static double g_sq = 0;
static long g_n = 0;

static void on_sample(double s, void *)
{
    g_sq += s * s;
    g_n++;
}

int main()
{
    std::vector<AudioDeviceInfo> devs = audio_input_devices();
    if (devs.empty()) {
        fprintf(stderr, "no input devices\n");
        return 1;
    }
    unsigned int id = devs[0].id;
    for (const AudioDeviceInfo &d : devs)   /* prefer BlackHole */
        if (d.name.find("BlackHole") != std::string::npos)
            id = d.id;

    LiveAudio audio;
    int fails = 0;
    for (int cycle = 1; cycle <= 3; cycle++) {
        std::string err;
        bool ok = audio.start(id, on_sample, 0, &err);
        fprintf(stderr, "cycle %d: start %s%s%s\n", cycle,
                ok ? "ok" : "FAILED",
                err.empty() ? "" : " (", err.empty() ? "" : (err + ")").c_str());
        if (!ok) {
            fails++;
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        audio.stop();
        fprintf(stderr, "cycle %d: stopped, %ld samples, rms %.0f\n",
                cycle, g_n, g_n ? sqrt(g_sq / g_n) : 0.0);
        g_sq = 0;
        g_n = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    printf("%s\n", fails ? "FAIL: restart(s) failed" : "OK: 3 restarts");
    if (fails)
        return 1;

    /* device switch: close + open a different device */
    if (devs.size() > 1) {
        unsigned int other = devs[0].id == id ? devs[1].id : devs[0].id;
        std::string err;
        if (!audio.start(other, on_sample, 0, &err)) {
            fprintf(stderr, "switch: FAILED (%s)\n", err.c_str());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        audio.stop();
        fprintf(stderr, "switch to device %u: ok\n", other);
        /* and back */
        if (!audio.start(id, on_sample, 0, &err)) {
            fprintf(stderr, "switch back: FAILED (%s)\n", err.c_str());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        audio.stop();
        fprintf(stderr, "switch back: ok\n");
    }
    printf("OK: restarts + device switch\n");
    return 0;
}
