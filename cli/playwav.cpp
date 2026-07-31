/* playwav - play a WAV file to a chosen audio output device (RtAudio).
 *
 * DEV AID for testing live reception through a loopback driver (e.g.
 * BlackHole): play a WEFAX recording into the loopback device, then
 * capture it in isobar-gui with Scan (or --test-scan) pointed at the
 * same device. Verifies the whole live audio path without a radio.
 *
 * Usage: ./playwav --list
 *        ./playwav file.wav <device-id>
 * Device ids are RtAudio ids from ./playwav --list (NOT the list
 * index shown by isobar-gui --list-devices).
 */
#include "../core/wavfile.h"
#include "../core/resample.h"
#include "../gui/rtaudio_compat.h"

#include <RtAudio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

static std::vector<int16_t> g_data;
static size_t g_pos = 0;

static int out_cb(void *output, void *, unsigned int nframes, double,
                  RtAudioStreamStatus, void *)
{
    int16_t *out = (int16_t *)output;
    size_t left = g_data.size() - g_pos;
    unsigned int n = nframes < left ? nframes : (unsigned int)left;
    memcpy(out, g_data.data() + g_pos, n * sizeof(int16_t));
    g_pos += n;
    if (n < nframes) {
        memset(out + n, 0, (nframes - n) * sizeof(int16_t));
        return 1;   /* done: stop the stream */
    }
    return 0;
}

int main(int argc, char **argv)
{
    /* Default API: RtAudio picks the best compiled-in backend per platform
     * (CoreAudio on macOS, ALSA/Pulse on Linux, WASAPI/DS on Windows). */
    RtAudio rt;

    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        for (unsigned int id : isobar_device_ids(rt)) {
            RtAudio::DeviceInfo inf = rt.getDeviceInfo(id);
            if (inf.outputChannels == 0)
                continue;
            printf("%u: %s (%u out ch, pref %u Hz)%s\n", id,
                   inf.name.c_str(), inf.outputChannels,
                   inf.preferredSampleRate,
                   inf.isDefaultOutput ? " [default]" : "");
        }
        return 0;
    }

    if (argc != 3) {
        fprintf(stderr, "usage: %s --list | %s file.wav <device-id>\n",
                argv[0], argv[0]);
        return 2;
    }

    unsigned int dev_id = (unsigned int)atoi(argv[2]);
    RtAudio::DeviceInfo inf = rt.getDeviceInfo(dev_id);
    if (inf.outputChannels == 0) {
        fprintf(stderr, "error: device %u has no output channels\n", dev_id);
        return 1;
    }
    unsigned int rate = inf.preferredSampleRate
                        ? inf.preferredSampleRate : 48000;

    try {
        /* WAV -> 22050 mono -> device rate */
        std::vector<double> audio = wav_read_22050(argv[1], 0);
        std::vector<double> rsbuf;
        Resampler rs;
        rs.init(22050.0, (double)rate);
        for (double s : audio)
            rs.feed(s, rsbuf);
        g_data.reserve(rsbuf.size());
        for (double s : rsbuf) {
            long v = lround(s);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            g_data.push_back((int16_t)v);
        }
        fprintf(stderr, "playing %.1f s to '%s' at %u Hz\n",
                g_data.size() / (double)rate, inf.name.c_str(), rate);

        RtAudio::StreamParameters prm;
        prm.deviceId = dev_id;
        prm.nChannels = 1;
        prm.firstChannel = 0;
        unsigned int frames = 2048;
        std::string aerr;
        /* playwav opens an OUTPUT stream (first arg), so the input-only shim
         * doesn't fit; inline the v5/v6 split here. */
#if defined(RTAUDIO_VERSION_MAJOR) && RTAUDIO_VERSION_MAJOR >= 6
        RtAudioErrorType e1 = rt.openStream(&prm, nullptr, RTAUDIO_SINT16, rate,
                                            &frames, &out_cb, nullptr, nullptr);
        bool ok = (e1 == RTAUDIO_NO_ERROR) &&
                  (rt.startStream() == RTAUDIO_NO_ERROR);
        if (!ok) aerr = rt.getErrorText();
#else
        bool ok = true;
        try {
            rt.openStream(&prm, nullptr, RTAUDIO_SINT16, rate, &frames,
                          &out_cb, nullptr);
            rt.startStream();
        } catch (const RtAudioError &e) {
            ok = false;
            aerr = e.what();
        }
#endif
        if (!ok) {
            fprintf(stderr, "error: %s\n", aerr.c_str());
            return 1;
        }
        while (rt.isStreamRunning())
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (rt.isStreamOpen())
            rt.closeStream();
    } catch (const std::exception &e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    fprintf(stderr, "done\n");
    return 0;
}
