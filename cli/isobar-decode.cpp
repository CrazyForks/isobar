/* isobar-decode - decode a WEFAX audio WAV into a grayscale fax image (PGM).
 *
 * Usage: ./isobar-decode [--60] [--fit-rate] input.wav out.pgm
 *
 * Pipeline: WAV (16-bit PCM, any rate ≥ 6000 Hz) -> 22050 Hz mono
 *           -> FM demod (8000 S/s video bytes) -> sync scan -> 1500 px lines
 *           -> binary PGM (P5).
 * --60 selects 60 rpm mode: the video stream is halved to 4000 S/s
 * before the sync scan (docs/01-program-analysis.md sec. 3.2(6)).
 */
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/synfile.h"
#include "../core/ratefit.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>

static void decode_progress(double done, double total)
{
    fprintf(stderr, "\rdecoding: %.0f / %.0f s", done, total);
    fflush(stderr);
}

int main(int argc, char **argv)
{
    int arg = 1;
    bool rpm60 = false;
    bool fit_rate = false;
    for (; arg < argc; arg++) {
        if (strcmp(argv[arg], "--60") == 0)
            rpm60 = true;
        else if (strcmp(argv[arg], "--fit-rate") == 0)
            fit_rate = true;
        else
            break;
    }
    if (argc - arg != 2) {
        fprintf(stderr,
                "usage: %s [--60] [--fit-rate] input.wav out.pgm|out.syn\n"
                "  --fit-rate  measure the line period from the picture and\n"
                "              correct the receiver's clock error. For\n"
                "              recordings that start mid-chart, where there\n"
                "              is no phasing preamble to measure it from.\n",
                argv[0]);
        return 2;
    }
    const char *in_path = argv[arg];
    const char *out_path = argv[arg + 1];

    try {
        std::string info;
        std::vector<double> audio = wav_read_22050(in_path, &info);
        fprintf(stderr, "input: %s\n", info.c_str());

        std::vector<uint8_t> video = fm_decode(audio, decode_progress);
        fprintf(stderr, "\rdemodulated: %.1f s of video (%zu samples)\n",
                video.size() / 8000.0, video.size());

        if (rpm60)
            video = video_halve_rate(video);

        /* Clock-error correction (core/ratefit.h). Measured from this
         * recording's own picture, never assumed; if the fold curve is
         * flat the rate is not determined and nothing is applied. */
        if (fit_rate) {
            RateFit rf = fit_line_period(video);
            fprintf(stderr, "rate fit: period %.2f samples (%+.0f ppm), "
                            "peak +-%.2f samples\n",
                    rf.period, rf.ppm, rf.halfw);
            if (rf.ok) {
                video = video_retime(video, rf.period);
                fprintf(stderr, "rate fit: applied (%+.0f px of skew "
                                "removed over %zu lines)\n",
                        -rf.ppm / 1e6 * 4000.0 * (video.size() / 4000.0)
                            * 3.0 / 8.0,
                        video.size() / 4000);
            } else {
                fprintf(stderr, "rate fit: NOT applied - the picture does "
                                "not determine the rate\n");
            }
        }

        FaxImage img = scan_lines(video);
        if (img.lines.empty()) {
            fprintf(stderr, "error: no sync pulses found - not a %s "
                            "WEFAX signal?\n", rpm60 ? "60 rpm" : "120 rpm");
            return 1;
        }

        int total = img.lines_locked + img.lines_corrected +
                    img.lines_coasted;
        fprintf(stderr,
                "lines: %zu (locked %d, corrected %d, coasted %d, "
                "lock ratio %.1f%%, relocks %d)\n",
                img.lines.size(), img.lines_locked, img.lines_corrected,
                img.lines_coasted,
                total ? 100.0 * img.lines_locked / total : 0.0, img.relocks);

        /* output format picked by extension: .syn -> KG-FAX compatible,
         * anything else -> PGM for eyeballing. Case-insensitive: ".SYN"
         * comes off Windows and from some file managers, and silently
         * writing a PGM under that name is the wrong answer. */
        std::string out = out_path;
        bool want_syn = false;
        if (out.size() >= 4) {
            std::string ext = out.substr(out.size() - 4);
            for (size_t i = 0; i < ext.size(); i++)
                ext[i] = (char)tolower((unsigned char)ext[i]);
            want_syn = ext == ".syn";
        }
        if (want_syn) {
            syn_write(out, img);
        } else {
            std::ofstream f(out_path, std::ios::binary);
            if (!f) {
                fprintf(stderr, "error: cannot write '%s'\n", out_path);
                return 1;
            }
            f << "P5\n" << FaxImage::WIDTH << " " << img.lines.size()
              << "\n255\n";
            for (auto &line : img.lines)
                f.write((const char *)line.data(), (std::streamsize)line.size());
            if (!f) {
                fprintf(stderr, "error: cannot write '%s'\n", out_path);
                return 1;
            }
        }
        fprintf(stderr, "wrote %s (%d x %zu)\n", out_path,
                FaxImage::WIDTH, img.lines.size());
    } catch (const std::exception &e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
