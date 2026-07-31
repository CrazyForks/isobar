/* audio.h - live audio input via RtAudio (GUI side; the core stays
 * dependency-free).
 *
 * Opens an input device and delivers 22050 Hz mono samples (int16
 * range, as doubles) to a callback - the same format wav_read_22050
 * produces, so FmDecoder feeds on it unchanged. Devices that won't do
 * 22050 Hz are opened at their preferred rate and converted with
 * core/resample.h.
 *
 * The sample callback runs on RtAudio's realtime thread: short work
 * only (our DSP is fine); UI updates must go through Fl::awake().
 */
#ifndef ISOBAR_AUDIO_H
#define ISOBAR_AUDIO_H

#include <string>
#include <vector>

struct AudioDeviceInfo {
    unsigned int id;            /* RtAudio device id */
    std::string  name;
    unsigned int in_channels;
    bool         is_default;
};

/* All devices with at least one input channel. Empty on error. */
std::vector<AudioDeviceInfo> audio_input_devices();

struct LiveAudio {
    LiveAudio();
    ~LiveAudio();              /* calls close() */

    /* Open the device (id from audio_input_devices) and start capture.
     * sample_cb(s, ud) is called per 22050 Hz sample on the audio
     * thread. On failure returns false and sets err.
     * Streams are kept open once created and only paused on stop():
     * RtAudio/CoreAudio leaks the device disconnect listener on
     * closeStream, so a closed device can never be reopened in the
     * same process ("setting disconnect listener" errors). */
    bool start(unsigned int device_id,
               void (*sample_cb)(double sample, void *ud), void *ud,
               std::string *err);
    void stop();     /* pause capture; the stream stays open */
    void close();    /* real teardown of all streams (quit) */

    bool running() const;

    /* impl details are pimpl'd so RtAudio headers stay out of the GUI */
    struct Impl;
    Impl *impl;
};

#endif
