/* isobar-decode - decode a WEFAX audio WAV into a grayscale fax image (PGM).
 *
 * Usage: ./isobar-decode [--60] input.wav out.pgm
 *
 * Pipeline: WAV (16-bit PCM, 22050/44100 Hz) -> 22050 Hz mono
 *           -> FM demod (8000 S/s video bytes) -> sync scan -> 1500 px lines
 *           -> binary PGM (P5).
 * --60 selects 60 rpm mode: the video stream is halved to 4000 S/s
 * before the sync scan (docs/01-program-analysis.md sec. 3.2(6)).
 */
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/synfile.h"

#include <cstdio>
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
    if (arg < argc && strcmp(argv[arg], "--60") == 0) {
        rpm60 = true;
        arg++;
    }
    if (argc - arg != 2) {
        fprintf(stderr, "usage: %s [--60] input.wav out.pgm|out.syn\n",
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
         * anything else -> PGM for eyeballing */
        std::string out = out_path;
        bool want_syn = out.size() >= 4 &&
                        out.compare(out.size() - 4, 4, ".syn") == 0;
        if (want_syn) {
            syn_write(out, img);
        } else {
            FILE *fp = fopen(out_path, "wb");
            if (!fp) {
                fprintf(stderr, "error: cannot write '%s'\n", out_path);
                return 1;
            }
            fprintf(fp, "P5\n%d %zu\n255\n", FaxImage::WIDTH, img.lines.size());
            for (auto &line : img.lines)
                fwrite(line.data(), 1, line.size(), fp);
            fclose(fp);
        }
        fprintf(stderr, "wrote %s (%d x %zu)\n", out_path,
                FaxImage::WIDTH, img.lines.size());
    } catch (const std::exception &e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
