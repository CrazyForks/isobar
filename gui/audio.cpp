/* audio.cpp - see audio.h. */

#include "audio.h"
#include "rtaudio_compat.h"
#include "../core/resample.h"

#include <RtAudio.h>

#include <map>

std::vector<AudioDeviceInfo> audio_input_devices()
{
    std::vector<AudioDeviceInfo> out;
    /* Default API: RtAudio picks the best compiled-in backend per platform
     * (CoreAudio on macOS, ALSA/Pulse on Linux, WASAPI/DS on Windows) —
     * avoids hardcoding one platform's API enum. */
    /* A broken audio subsystem must not kill the app: the v6 ctor throws
     * when no backend is usable and v5's getDeviceInfo throws per device.
     * Return what we have (possibly empty) and let the caller alert. */
    try {
        RtAudio rt;
        for (unsigned int id : isobar_device_ids(rt)) {
            RtAudio::DeviceInfo inf = rt.getDeviceInfo(id);
            if (inf.inputChannels == 0)
                continue;
            AudioDeviceInfo d;
            d.id = id;
            d.name = inf.name;
            d.in_channels = inf.inputChannels;
            d.is_default = inf.isDefaultInput;
            out.push_back(d);
        }
    } catch (const std::exception &) {
    }
    return out;
}

/* one capture stream per device, kept open once created.
 *
 * WHY: RtAudio/CoreAudio leaks the device disconnect listener on
 * closeStream (v6.0.1), so a device that has been opened once can never
 * be opened again in the same process ("system error setting
 * disconnect listener"). Keeping each stream open and only pausing it
 * sidesteps the leak entirely; teardown happens at process exit. */
struct Stream {
    RtAudio *rt = nullptr;
    Resampler rs;          /* used when capture rate != 22050 */
    bool      needs_rs = false;
    double    cap_rate = 22050.0;
    void (*cb)(double, void *) = nullptr;
    void *ud = nullptr;
    std::vector<double> rsbuf;

    static int rt_cb(void *, void *input, unsigned int nframes,
                     double, RtAudioStreamStatus, void *userdata)
    {
        Stream *self = (Stream *)userdata;
        const int16_t *in = (const int16_t *)input;
        for (unsigned int i = 0; i < nframes; i++) {
            double s = (double)in[i];
            if (self->needs_rs) {
                self->rsbuf.clear();
                self->rs.feed(s, self->rsbuf);
                for (double r : self->rsbuf)
                    self->cb(r, self->ud);
            } else {
                self->cb(s, self->ud);
            }
        }
        return 0;
    }
};

struct LiveAudio::Impl {
    std::map<unsigned int, Stream *> streams;
    Stream *current = nullptr;
};

LiveAudio::LiveAudio()
{
    impl = new Impl;
}

LiveAudio::~LiveAudio()
{
    close();
    delete impl;
}

static Stream *open_stream(unsigned int device_id, std::string *err)
{
    Stream *s = new Stream;
    s->rt = new RtAudio();   /* default API — see audio_input_devices() */

    RtAudio::StreamParameters prm;
    prm.deviceId = device_id;
    prm.nChannels = 1;
    prm.firstChannel = 0;
    unsigned int frames = 2048;   /* ~100 ms at 22050 Hz */

    /* first try 22050 Hz directly (Core Audio often converts); fall
     * back to the device's preferred rate + our resampler. isobar_open_input_stream
     * normalizes the v5 (throws) / v6 (returns error) openStream APIs. */
    if (!isobar_open_input_stream(*s->rt, &prm, RTAUDIO_SINT16, 22050, &frames,
                                  &Stream::rt_cb, s, err)) {
        unsigned int pref = 48000;
        try {
            RtAudio::DeviceInfo inf = s->rt->getDeviceInfo(device_id);
            if (inf.preferredSampleRate)
                pref = inf.preferredSampleRate;
        } catch (const std::exception &) {
            /* v5 throws here; keep the 48 kHz guess */
        }
        std::string err2;
        if (!isobar_open_input_stream(*s->rt, &prm, RTAUDIO_SINT16, pref,
                                      &frames, &Stream::rt_cb, s, &err2)) {
            if (err) *err = err2;
            delete s->rt;
            delete s;
            return nullptr;
        }
        s->rs.init((double)pref, 22050.0);
        s->needs_rs = true;
        s->cap_rate = (double)pref;
    }
    return s;
}

bool LiveAudio::start(unsigned int device_id,
                      void (*sample_cb)(double, void *), void *ud,
                      std::string *err)
{
    Stream *s = impl->streams[device_id];
    if (!s) {
        s = open_stream(device_id, err);
        if (!s)
            return false;
        impl->streams[device_id] = s;
    }
    s->cb = sample_cb;
    s->ud = ud;

    /* only one stream runs at a time; if the old stream refuses to
     * stop, starting the new one would leave two feeding callbacks */
    if (impl->current && impl->current != s &&
        impl->current->rt->isStreamRunning() &&
        !isobar_stop_stream(*impl->current->rt, err))
        return false;

    if (!s->rt->isStreamRunning() &&
        !isobar_start_stream(*s->rt, err)) {
        return false;
    }
    impl->current = s;
    return true;
}

void LiveAudio::stop()
{
    /* pause only: keep the stream open (see Stream comment above);
     * best-effort - nothing to do with a failure here */
    if (impl->current && impl->current->rt->isStreamRunning())
        isobar_stop_stream(*impl->current->rt, nullptr);
}

void LiveAudio::close()
{
    for (auto &kv : impl->streams) {
        Stream *s = kv.second;
        if (s->rt->isStreamRunning())
            isobar_stop_stream(*s->rt, nullptr);
        if (s->rt->isStreamOpen())
            s->rt->closeStream();
        delete s->rt;
        delete s;
    }
    impl->streams.clear();
    impl->current = nullptr;
}

bool LiveAudio::running() const
{
    return impl->current && impl->current->rt->isStreamRunning();
}
