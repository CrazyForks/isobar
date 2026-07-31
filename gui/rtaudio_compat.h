/* rtaudio_compat.h - paper over the RtAudio v5 vs v6 API split.
 *
 * WHY: RtAudio 6.0 changed the API in incompatible ways, and the two package
 * ecosystems ship different majors — Homebrew has 6.x, Debian/Ubuntu's
 * `librtaudio-dev` has 5.x. Both compile against <RtAudio.h>, so a few
 * shims let the same source build on either. The version is exposed via
 * RTAUDIO_VERSION_MAJOR.
 *
 * v5 → v6 changes this papers over:
 *   - getDeviceCount() + getDeviceInfo(id)        vs  getDeviceIds()
 *   - openStream() throws RtAudioError on failure  vs  returns RtAudioErrorType
 *   - startStream() throws                         vs  returns RtAudioErrorType
 *   - (no RTAUDIO_NO_ERROR)                        vs  RTAUDIO_NO_ERROR
 *   - error text in the exception's what()         vs  getErrorText()
 */
#ifndef ISOBAR_RTAUDIO_COMPAT_H
#define ISOBAR_RTAUDIO_COMPAT_H

#include <RtAudio.h>
#include <string>
#include <vector>

/* The list of device ids. v6's getDeviceIds() does this directly; v5 needs
 * a loop over getDeviceCount()/getDeviceInfo(). */
inline std::vector<unsigned int> isobar_device_ids(RtAudio &rt)
{
#if defined(RTAUDIO_VERSION_MAJOR) && RTAUDIO_VERSION_MAJOR >= 6
    return rt.getDeviceIds();
#else
    std::vector<unsigned int> ids;
    int n = rt.getDeviceCount();
    for (int i = 0; i < n; i++)
        ids.push_back((unsigned int)i);
    return ids;
#endif
}

/* Open an input-only stream, returning true + setting err on failure (instead
 * of v5's throw). The v6 branch checks the returned RtAudioErrorType; the v5
 * branch catches the RtAudioError thrown on failure. */
inline bool isobar_open_input_stream(RtAudio &rt, RtAudio::StreamParameters *in,
                                     RtAudioFormat fmt, unsigned int rate,
                                     unsigned int *frames, RtAudioCallback cb,
                                     void *userdata, std::string *err)
{
#if defined(RTAUDIO_VERSION_MAJOR) && RTAUDIO_VERSION_MAJOR >= 6
    RtAudioErrorType e = rt.openStream(nullptr, in, fmt, rate, frames, cb,
                                       userdata, nullptr);
    if (e != RTAUDIO_NO_ERROR) {
        if (err) *err = rt.getErrorText();
        return false;
    }
    return true;
#else
    try {
        rt.openStream(nullptr, in, fmt, rate, frames, cb, userdata);
    } catch (const RtAudioError &e) {
        if (err) *err = e.what();
        return false;
    }
    return true;
#endif
}

/* Start the stream, returning true + setting err on failure. */
inline bool isobar_start_stream(RtAudio &rt, std::string *err)
{
#if defined(RTAUDIO_VERSION_MAJOR) && RTAUDIO_VERSION_MAJOR >= 6
    if (rt.startStream() != RTAUDIO_NO_ERROR) {
        if (err) *err = rt.getErrorText();
        return false;
    }
    return true;
#else
    try {
        rt.startStream();
    } catch (const RtAudioError &e) {
        if (err) *err = e.what();
        return false;
    }
    return true;
#endif
}

#endif
