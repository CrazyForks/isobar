/* main.cpp - Isobar GUI (FLTK).
 *
 * Layout follows the original main form exactly, using the coordinates
 * extracted from the original binary's form resources
 * (docs/05-gui-layout.md, Form1; client area 888x551).
 * Captions are English (DEVIATIONS.md #5); the Japanese->English
 * mapping table is in docs/05-gui-layout.md.
 *
 * Functional so far:
 *   Load data  -> open .syn file (menu item 1)
 *   [dev] WAV  -> decode a WAV through the batch pipeline in core/
 *                 (menu item 2 of the same button; DEV AID, kept for
 *                 offline testing)
 *   Save data  -> save .syn file
 *   Clear      -> clear the preview
 *   Details... -> Form4 settings dialog, persisted to isobar.ini (next
 *                 to the executable, like the original's kgfax.ini)
 *   [menu btn] -> Form9 input-device chooser (WaveDev setting)
 *   Scan       -> start/stop recording lines into the image
 *   Auto ctl   -> arm tone control: 300 Hz start tone presses Scan,
 *                 450 Hz stop tone releases it (docs/01 sec. 3.2(5))
 *   Auto save  -> opens Form8 on press; on stop tone, save
 *                 DirName/YYYYMMDDHHMM.syn (exe dir if DirName empty)
 *   Quit       -> save settings (rpm/syn choices + window position) to
 *                 isobar.ini, then quit (same on title-bar close)
 * Audio capture (RtAudio) runs from program start, like the original
 * (docs/01 sec. 3.1): the scope, LEDs and tone detectors are always
 * live; Scan only gates whether lines go into the image.
 *
 * Scope (docs/01 sec. 3.3): during a decode the worker hands each
 * 4096-sample BPF block to the UI via Fl::awake(); the FFT runs on
 * the UI thread and the result goes to the ScopeView. Status LEDs
 * (docs/01 sec. 3.2(8), line states from scan_lines):
 *   locked    (shape edge):  Sync det green,  Sync corr off
 *   corrected (fallback):    Sync det green,  Sync corr cyan
 *   coasting  (predicted):   Sync det red,    Sync corr cyan
 * Control LED: 300 Hz start tone -> green, 450 Hz stop tone -> orange
 * (docs/01 sec. 3.2(5); level over threshold for DetTime blocks).
 *
 * Decode runs on a worker thread and hands the finished image back
 * with Fl::awake(); only the UI thread touches widgets (deviation #1
 * in DEVIATIONS.md). Decode progress is reported in the window title;
 * the original's Form2 progress dialog (gui/progressdialog.*) covers
 * the save/print/auto-save renders instead.
 *
 * Usage: ./isobar-gui [file.syn]
 *        ./isobar-gui --test-scope [file.wav]
 *        ./isobar-gui --test-color [file.wav]
 *        ./isobar-gui --test-palette <mode> [invert] [file.wav]
 *        ./isobar-gui --test-export <kind> <portrait> <mode> [invert]
 *        ./isobar-gui --test-print [file.syn]
 *        ./isobar-gui --test-autosave-dialog [file.syn]
 *        ./isobar-gui --test-zoom N [file.syn]
 *        ./isobar-gui --test-rotate90 [file.syn]
 *        ./isobar-gui --test-rotate180 [file.syn]
 *        ./isobar-gui --test-notice
 *        ./isobar-gui --test-progress
 *        ./isobar-gui --test-autosave [file.syn]
 *        ./isobar-gui --test-details
 *        ./isobar-gui --test-quit-save
 *        ./isobar-gui --dump-settings
 *        ./isobar-gui --list-devices
 *        ./isobar-gui --test-scan [device] [seconds] [auto]
 *   --test-scope (DEV AID): starts a WAV decode immediately, as if
 *     Load data > [dev] Decode WAV had been picked. Default input is
 *     "jmh sample.wav".
 *   --test-waterfall was removed 2026-07-30: the scope now always
 *     shows spectrum + waterfall together, like the original.
 *   --test-color (DEV AID): decode the sample, then open the Color
 *     dialog and apply the result.
 *   --test-palette <mode> [invert] [file.wav] (DEV AID): decode with a
 *     palette pre-applied.
 *   --test-export <kind> <portrait> <mode> [invert] (DEV AID): decode,
 *     then run the Save-image render path to /tmp/isobar-export.bmp.
 *   --test-rotate90 / --test-rotate180 (DEV AID): load out.syn, apply
 *     the Vertical (90-degree) / XY-flip (180-degree) operation once;
 *     window stays up for screenshot testing.
 *   --test-zoom N (DEV AID): load out.syn, zoom the preview to level
 *     N (1 = 1/2, 2 = 1:1) as if center-clicked; window stays up.
 *   --test-print (DEV AID): render the exact print bitmap (raster,
 *     palette, full buffer) to /tmp/print-test.bmp and exit - no
 *     Fl_Printer (the native panel can't be driven headlessly).
 *   --test-autosave-dialog (DEV AID): open the Form8 auto-save
 *     settings dialog over the main window; stays up for screenshots.
 *   --test-notice (DEV AID): pop the Form10 recording-notice for 2 s,
 *     then dismiss it (visual check of the device-switch popup).
 *   --test-progress (DEV AID): animate the Form2 progress bar 0 -> 1
 *     over ~1 s (visual check of the render progress dialog).
 *   --test-autosave [file.syn] [fmt] [size] (DEV AID): load out.syn, arm
 *     auto-save (CycleGet) at /tmp, fire auto_save() once, report on
 *     stderr. fmt 0=.syn (default) / 1=.bmp; size 0..2 = bmp render scale.
 *   --test-details (DEV AID): opens the Details dialog at startup.
 *   --test-quit-save (DEV AID): runs the Quit path (settings write)
 *     immediately, so the ini write can be tested without clicking.
 *   --dump-settings (DEV AID): prints the effective settings as ini
 *     to stdout and exits (no window).
 */
#include "faxview.h"
#include "scopeview.h"
#include "settingsdialog.h"
#include "devicedialog.h"
#include "colordialog.h"
#include "exportdialog.h"
#include "autosavedialog.h"
#include "noticedialog.h"
#include "progressdialog.h"
#include "audio.h"
#include "../core/synfile.h"
#include "../core/wavfile.h"
#include "../core/decoder.h"
#include "../core/syncscan.h"
#include "../core/live.h"

#include <atomic>
#include "../core/fft.h"
#include "../core/settings.h"
#include "../core/tonedetect.h"
#include "../core/palette.h"
#include "../core/bmpfile.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Toggle_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Printer.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/fl_ask.H>

#include <thread>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>

/* localtime() is not thread-safe and MSVC deprecates it (C4996). The
 * reentrant replacement is spelled differently on Windows. */
static bool local_time(const time_t *t, struct tm *out)
{
#ifdef _WIN32
    return localtime_s(out, t) == 0;
#else
    return localtime_r(t, out) != 0;
#endif
}

/* LED colors */
static const Fl_Color LED_OFF  = fl_rgb_color(110, 110, 110);
static const Fl_Color LED_OK   = fl_rgb_color(0, 200, 0);
static const Fl_Color LED_BAD  = fl_rgb_color(120, 30, 30);
static const Fl_Color LED_CORR = fl_rgb_color(0, 200, 200);
static const Fl_Color LED_WARN = fl_rgb_color(220, 160, 0);

struct AppState {
    Fl_Double_Window *win;
    FaxView          *view;
    ScopeView        *scope;
    Fl_Box           *led_sync;    /* Sync det  */
    Fl_Box           *led_corr;    /* Sync corr */
    Fl_Box           *led_ctl;     /* Control   */
    Fl_Choice        *rpm_choice;
    Fl_Choice        *syn_choice;
    Fl_Toggle_Button *scan_btn;
    Fl_Toggle_Button *autoctl_btn;
    Fl_Toggle_Button *autosave_btn;
    Fl_Toggle_Button *sync_btn;
    KgSettings       settings;     /* UI thread only; copied to worker */
    FaxImage          image;       /* last image shown; UI thread only */
    bool              decoding;
    bool              scanning;    /* audio capture running */
    bool              recording;   /* lines go into the image (Scan) */
    bool              autoctl;     /* arm tone-triggered record      */
    bool              autosave;    /* armed: save on stop tone / max width */
    int               autosave_fmt;   /* Form8 format: 0=.syn 1=.bmp    */
    int               autosave_size;  /* Form8 image-size radio 0..2 (bmp) */
    int               colormode;   /* palette mode 0..3 (Form5)      */
    int               invert;      /* palette invert flag (Form5)    */
    uint8_t           pal[256][3]; /* built from colormode + invert  */
    int               export_kind;     /* Form6 kind, session-sticky */
    int               export_portrait; /* Form6 orientation          */
    FaxImage          backup;      /* pre-rotate buffer (Vertical btn) */
    bool              rotated90;   /* Button8 toggle flag (a1+1096)    */
    Fl_Button        *vertical_btn;   /* for the caption toggle      */
};

/* ---- small helpers ---- */

/* Window title doubles as the status line for now (dev aid). */
static void set_title(AppState *app, const char *suffix)
{
    /* std::string rather than a fixed buffer: the suffix is caller-built
     * (file paths, decode status) and gcc rightly warned that it could be
     * truncated into 160 bytes. copy_label() takes its own copy. */
    std::string title = "Isobar";
    if (suffix && suffix[0]) {
        title += " - ";
        title += suffix;
    }
    app->win->copy_label(title.c_str());
}

static void set_led(Fl_Box *led, Fl_Color c)
{
    led->color(c);
    led->redraw();
}

/* line state from scan_lines/LiveScan: 0 locked / 1 corrected /
 * 2 coasting (docs/01 sec. 3.2(8)) / 3 tracking off (Sync button) */
static void set_leds(AppState *app, int state)
{
    if (state == 3) {   /* Sync off: both LEDs black (original behavior) */
        set_led(app->led_sync, LED_OFF);
        set_led(app->led_corr, LED_OFF);
        return;
    }
    set_led(app->led_sync, state == 2 ? LED_BAD : LED_OK);
    set_led(app->led_corr, state == 0 ? LED_OFF : LED_CORR);
    /* led_ctl (tone detection) is driven separately via tone_levels */
}

static void set_leds_idle(AppState *app)
{
    set_led(app->led_sync, LED_OFF);
    set_led(app->led_corr, LED_OFF);
    set_led(app->led_ctl,  LED_OFF);
}

/* rebuild the palette from colormode/invert and hand it to the view
 * (re-render is a separate set_image call, like the original's
 * Button10 re-render after the Form5 OK) */
static void apply_palette(AppState *app)
{
    palette_build(app->colormode, app->invert, app->pal);
    app->view->set_palette(app->pal);
}

static void show_image(AppState *app)
{
    app->view->set_image(app->image);
    char buf[96];
    snprintf(buf, sizeof buf, "%zu lines", app->image.lines.size());
    set_title(app, buf);
}

/* defined in the Vertical/XY-flip section below */
static void reset_rotate(AppState *app);

static void load_syn(AppState *app, const char *path)
{
    SynHeader hdr;
    FaxImage img = syn_read(path, &hdr);   /* throws on error */
    app->image = img;
    reset_rotate(app);
    /* the .syn header carries the palette mode + invert (docs/01
     * sec. 4); restore them like the original does */
    app->colormode = hdr.mode;
    app->invert = hdr.negative ? 1 : 0;
    apply_palette(app);
    app->view->reset_zoom();   /* original resets zoom on .syn load */
    set_leds_idle(app);
    app->scope->clear();
    show_image(app);
}

/* ---- Load data (open .syn) ---- */

static void cb_open_syn(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    const char *path = fl_file_chooser("Open .syn file",
                                       "SynFax files (*.syn)\t*.syn", "");
    if (!path) return;
    try {
        load_syn(app, path);
    } catch (const std::exception &e) {
        fl_alert("cannot load %s:\n%s", path, e.what());
    }
}

/* ---- Save data (save .syn) ---- */

static void cb_save_syn(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    if (app->image.lines.empty()) {
        fl_alert("no image to save");
        return;
    }
    Fl_Native_File_Chooser nc;
    nc.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    nc.title("Save .syn file");
    nc.filter("SynFax files\t*.syn\n");
    nc.preset_file("fax.syn");
    if (nc.show() != 0) return;   /* cancelled */

    std::string path = nc.filename();
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".syn") != 0)
        path += ".syn";
    try {
        syn_write(path, app->image, (uint8_t)app->colormode,
                  (uint8_t)app->invert);
        set_title(app, "saved");
    } catch (const std::exception &e) {
        fl_alert("cannot save %s:\n%s", path.c_str(), e.what());
    }
}

/* ---- Save image (BMP export, Form6 + Form3 flow) ---- */

/* box-average render of img at the given scale through the palette
 * (docs/01 sec. 4: kinds 1..3 = 3x3 / 2x2 / 1x1 renders) */
static void render_export_rgb(const FaxImage &img, int scale,
                              const uint8_t pal[256][3],
                              std::vector<uint8_t> &out, int *ow, int *oh)
{
    const int sw = FaxImage::WIDTH;
    const int sh = (int)img.lines.size();
    int w = sw / scale;
    int h = (sh + scale - 1) / scale;
    out.assign((size_t)w * h * 3, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sum = 0, n = 0;
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++) {
                    int sy = y * scale + dy, sx = x * scale + dx;
                    if (sy < sh && sx < sw) {
                        sum += img.lines[sy][sx];
                        n++;
                    }
                }
            const uint8_t *c = pal[n ? sum / n : 0];
            uint8_t *px = &out[((size_t)y * w + x) * 3];
            px[0] = c[0];
            px[1] = c[1];
            px[2] = c[2];
        }
    }
    *ow = w;
    *oh = h;
}

/* 90-degree counter-clockwise rotation of a packed RGB buffer (the
 * "Land." export; the spec only says "transposed" - the direction was
 * a guess until user testing 2026-07-31 showed CW gives an upside-down
 * export of a sideways-transmitted JMH chart, CCW the upright one) */
static void rotate90ccw(std::vector<uint8_t> &rgb, int *w, int *h)
{
    int sw = *w, sh = *h;
    std::vector<uint8_t> out(rgb.size());
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            int nx = y, ny = sw - 1 - x;
            uint8_t *d = &out[((size_t)ny * sh + nx) * 3];
            const uint8_t *s = &rgb[((size_t)y * sw + x) * 3];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    rgb.swap(out);
    *w = sh;
    *h = sw;
}

static void cb_save_image(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    if (app->image.lines.empty()) {
        fl_alert("no image to save");
        return;
    }
    /* Form6: kind + orientation (session-sticky, like the original) */
    if (!export_dialog_run(&app->export_kind, &app->export_portrait))
        return;

    Fl_Native_File_Chooser nc;
    nc.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    nc.title("Save image");
    nc.filter("Bitmap files\t*.bmp\n");
    nc.preset_file("fax.bmp");
    if (nc.show() != 0) return;   /* cancelled */
    std::string path = nc.filename();
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".bmp") != 0)
        path += ".bmp";

    /* kinds 1..3 = 3x3 / 2x2 / 1x1 box-average renders */
    static const int scale_of[4] = { 1, 3, 2, 1 };
    std::vector<uint8_t> rgb;
    int w, h;
    {   /* Form2 progress dialog around the render (docs/01 sec. 4) */
        ProgressScope ps(app->win->x(), app->win->y(),
                         app->win->w(), app->win->h());
        render_export_rgb(app->image, scale_of[app->export_kind],
                          app->pal, rgb, &w, &h);
        ps.update(1.0);
    }
    if (app->export_kind == 0) {
        /* kind 0 ("Current view", the original's own caption): the WHOLE
         * image as an upright raster scaled to 760 px wide, NOT the
         * sideways column view the preview actually shows. The label is
         * kept for fidelity - the original's first option is named just
         * as loosely (docs/01 sec. 4 "Save image"). */
        Fl_RGB_Image full(rgb.data(), w, h, 3);
        int dh = (int)((long)h * 760 / w);
        if (dh < 1) dh = 1;
        Fl_Image *scaled = full.copy(760, dh);
        const char *px = scaled->data()[0];
        rgb.assign(px, px + (size_t)760 * dh * 3);
        w = 760;
        h = dh;
        delete scaled;
    }
    /* "Land." = 90-degree rotated export, "Port." = raster as-is.
     * Deliberately SWAPPED from the original (横 = as-is, 縦 = rotated)
     * so the label matches the output shape (DEVIATIONS #14, user
     * decision 2026-07-31). */
    if (!app->export_portrait && app->export_kind != 0)
        rotate90ccw(rgb, &w, &h);
    try {
        bmp_write(path, rgb.data(), w, h);
        set_title(app, "image saved");
    } catch (const std::exception &e) {
        fl_alert("cannot save %s:\n%s", path.c_str(), e.what());
    }
}

/* ---- Print (Button7, docs/01 sec. 4 "Print") ---- */

/* the exact print bitmap: full RASTER (x = pixel in line, y = line),
 * palette applied, like the original. Renders the received
 * lines only - the original renders all 2280 lines with a black tail
 * (deliberate difference, noted in docs/01). */
static void render_print_rgb(const FaxImage &img, const uint8_t pal[256][3],
                             std::vector<uint8_t> &out, int *ow, int *oh)
{
    const int w = FaxImage::WIDTH;
    const int h = (int)img.lines.size();
    out.assign((size_t)w * h * 3, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *c = pal[img.lines[y][x]];
            uint8_t *px = &out[((size_t)y * w + x) * 3];
            px[0] = c[0];
            px[1] = c[1];
            px[2] = c[2];
        }
    *ow = w;
    *oh = h;
}

static void cb_print(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    if (app->image.lines.empty())
        return;
    /* print always uses the unrotated image (10264-10291) */
    const FaxImage &img = app->rotated90 ? app->backup : app->image;
    std::vector<uint8_t> rgb;
    int w, h;
    {   /* Form2 progress dialog around the raster render (docs/01 §4) */
        ProgressScope ps(app->win->x(), app->win->y(),
                         app->win->w(), app->win->h());
        render_print_rgb(img, app->pal, rgb, &w, &h);
        ps.update(1.0);
    }

    Fl_Printer printer;
    /* FLTK 1.4 renamed start_job/start_page → begin_job/begin_page (both
     * zero-arg). FLTK 1.3 has start_job(int pagecount, …) (page count required)
     * + start_page(void). Homebrew ships 1.4; Debian apt + vcpkg ship 1.3.
     * Branch on FL_API_VERSION so the same source builds on both. */
#if defined(FL_API_VERSION) && FL_API_VERSION >= 10400
    if (printer.begin_job() != 0)   /* native panel; cancel = no-op */
        return;
    if (printer.begin_page() == 0) {
#else
    if (printer.start_job(1) != 0)   /* 1 page; native panel; cancel = no-op */
        return;
    if (printer.start_page() == 0) {
#endif
        int pw, ph;
        printer.printable_rect(&pw, &ph);
        printer.origin(0, 0);
        /* the original stretches the FULL fixed 1500x2280 buffer over the
         * whole page - StretchDIBits(0,0,PageWidth,PageHeight,0,0,1500,2280)
         * (10336) - so a line is always ph/2280 tall however many were
         * received; the tail just comes out black. We skip the black tail
         * (see render_print_rgb), so we must scale by that same 2280-line
         * ruler and leave the rest of the page blank. Stretching h lines
         * over the whole page instead makes a partial reception 2280/h too
         * tall (user's 1157-line print came out ~2x stretched, 2026-08-01).
         * Width still fills the page, i.e. no aspect preservation, exactly
         * like the original. The scale must be baked into the image with
         * copy(): scaled Fl_RGB_Image::draw() is ignored by the macOS
         * printer driver (user test 2026-07-31 printed 1:1 - only the
         * top-left ~612x792 of the bitmap landed on the page) */
        int th = (int)((double)ph * h / FaxImage::MAX_LINES + 0.5);
        if (th < 1)  th = 1;
        if (th > ph) th = ph;    /* buffer past 2280 lines (.bmp autosave) */
        Fl_RGB_Image page(rgb.data(), w, h, 3);
        Fl_Image *scaled = page.copy(pw, th);
        scaled->draw(0, 0);
        delete scaled;
        printer.end_page();
    }
    printer.end_job();
}

/* ---- Color (Form5 color processing) ---- */

static void cb_color(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    int mode = app->colormode, inv = app->invert;
    if (!color_dialog_run(&mode, &inv))
        return;   /* Cancel: pending values discarded */
    app->colormode = mode;
    app->invert = inv;
    apply_palette(app);
    /* re-render the displayed image through the new palette (docs/01
     * sec. 4 Button10). During live reception the sideways view just
     * picks the palette up for new columns. */
    if (!app->recording && !app->image.lines.empty())
        app->view->set_image(app->image);
}

/* ---- Vertical / XY flip (Button8 / Button11 buffer rotations) ---- */

/* new image content abandons the rotate toggle (caption back to
 * "Vertical", backup dropped) */
static void reset_rotate(AppState *app)
{
    app->rotated90 = false;
    app->backup = FaxImage();
    if (app->vertical_btn)
        app->vertical_btn->copy_label("Vertical");
}

/* Button8 (docs/01 sec. 4 "Rotate"): IN-PLACE 90-degree rotate toggle,
 * not a window (the original's Form7 is orphaned - nothing opens it).
 * First click rotates into lines 390..1889 of a 1890-line image and
 * re-labels the button; second click restores the backup. */
static void cb_vertical(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    if (app->recording || app->image.lines.empty())
        return;
    if (app->rotated90) {
        app->image = app->backup;   /* restore the pre-rotate buffer */
        app->rotated90 = false;
    } else {
        app->backup = app->image;
        const FaxImage &old = app->backup;
        const int n = (int)old.lines.size();
        /* new[l][p] = old[1499-p][l-390] for l in [390,1890); lines
         * 0..389 stay black, nothing beyond 1889 (the original's
         * Vertical rotate; its pan offset is normally 0 and reset by
         * both paths, so it is not ported) */
        FaxImage rot;
        rot.lines.assign(1890, std::vector<uint8_t>(FaxImage::WIDTH, 0));
        for (int l = 390; l < 1890; l++)
            for (int p = 0; p < FaxImage::WIDTH; p++) {
                int src = 1499 - p;
                if (src < n)
                    rot.lines[l][p] = old.lines[src][l - 390];
            }
        app->image = rot;
        app->rotated90 = true;
    }
    /* caption toggle: exact original strings unverified (docs/01
     * sec. 4 "Rotate") */
    app->vertical_btn->copy_label(app->rotated90 ? "Horizontal"
                                                 : "Vertical");
    app->view->set_image(app->image);
}

/* Button11 (docs/01 sec. 4 "Rotate"): 180-degree rotation of the
 * received region - NOT a toggle (two clicks = identity). */
static void cb_xyflip(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    if (app->recording || app->image.lines.empty())
        return;
    /* clean 180: new[l][p] = old[N-1-l][1499-p] over the N received
     * lines. The original reads old[N-l][1500-p] - a one-line/one-pixel
     * shift quirk noted in docs/01, not replicated. */
    const int n = (int)app->image.lines.size();
    FaxImage old = app->image;
    for (int l = 0; l < n; l++)
        for (int p = 0; p < FaxImage::WIDTH; p++)
            app->image.lines[l][p] =
                old.lines[n - 1 - l][FaxImage::WIDTH - 1 - p];
    app->view->set_image(app->image);
}

/* ---- Clear (clear screen) ---- */

static void cb_clear(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    app->image = FaxImage();
    reset_rotate(app);
    app->view->reset_zoom();   /* Clear button resets zoom, like the original */
    if (app->recording)
        app->view->live_begin();   /* clear columns, stay in live mode */
    else
        app->view->set_image(app->image);
    app->scope->clear();
    set_leds_idle(app);
    set_title(app, 0);
}

/* ---- Details... (Form4 settings dialog) ---- */

static void cb_details(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    settings_dialog_run(app->settings);
}

/* rpm/syn combos: keep settings live (saved on quit; rpm also picks
 * the decode line rate: 120 or 60 rpm) */
static void cb_choice(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    app->settings.rpm = app->rpm_choice->value();
    app->settings.syn = app->syn_choice->value();
}

/* ---- [dev] Decode WAV (worker thread + Fl::awake) ---- */

/* messages worker -> UI, handed over via Fl::awake */
struct DecodeDone {
    AppState   *app;
    FaxImage   *img;      /* owned by the message; null on error */
    std::string error;
};

struct DecodeProg {
    AppState *app;
    double done, total;
};

struct ScopeMsg {
    AppState *app;
    double    block[FFT_N];
};

struct LedMsg {
    AppState *app;
    int       state;   /* 0 locked / 1 corrected / 2 coasting */
};

struct ToneMsg {
    AppState *app;
    int       state;   /* 0 none / 1 start 300 Hz / 2 stop 450 Hz */
};

static AppState *g_app;   /* for the C-style core callbacks */
static int g_last_led = -1;   /* last LED state posted (-1 = none) */
/* File-decode gate: while a WAV decode runs on its worker thread, the
 * (always-on) audio thread skips the scope + tone feeding so the two
 * do not race on g_streak3/4 / g_tone_state or double-feed the scope.
 * Written by the UI thread (start_decode / cb_decode_done), read by
 * the audio thread, so atomic like g_recording below. */
static std::atomic<bool> g_file_decode{false};

static void cb_progress(void *p)
{
    DecodeProg *msg = (DecodeProg *)p;
    char buf[96];
    snprintf(buf, sizeof buf, "decoding %.0f / %.0f s",
             msg->done, msg->total);
    set_title(msg->app, buf);
    delete msg;
}

static void decode_progress(double done, double total)
{
    /* runs on the worker thread: only Fl::awake(), never widget calls */
    if (g_app)
        Fl::awake(cb_progress, new DecodeProg{g_app, done, total});
}

static void cb_scope(void *p)
{
    ScopeMsg *msg = (ScopeMsg *)p;
    double db[FFT_BINS];
    fft_4096(msg->block, db);   /* ~microseconds, fine on the UI thread */
    msg->app->scope->set_spectrum(db);
    delete msg;
}

static void scope_block(const double bpf[FFT_N])
{
    /* runs on the worker thread (~5.4 Hz at 22050 S/s) */
    if (!g_app)
        return;
    ScopeMsg *msg = new ScopeMsg;
    msg->app = g_app;
    memcpy(msg->block, bpf, sizeof msg->block);
    Fl::awake(cb_scope, msg);
}

static void cb_led(void *p)
{
    LedMsg *msg = (LedMsg *)p;
    set_leds(msg->app, msg->state);
    delete msg;
}

static void line_state(int state)
{
    /* runs on the worker thread; post only on state change */
    if (!g_app || state == g_last_led)
        return;
    g_last_led = state;
    Fl::awake(cb_led, new LedMsg{g_app, state});
}

/* ---- Control LED: 300/450 Hz start/stop tones (docs/01 sec. 3.2(5)) ---- */

/* defined in the live-reception section below */
static void record_on(AppState *app);
static void record_off(AppState *app);
static void auto_save(AppState *app);

static int g_det_blocks = 1;   /* DetTime in 100 ms blocks, per decode */
static int g_tone_state = 0, g_streak3 = 0, g_streak4 = 0;

static void cb_tone(void *p)
{
    ToneMsg *msg = (ToneMsg *)p;
    AppState *app = msg->app;
    Fl_Color c = msg->state == 1 ? LED_OK
               : msg->state == 2 ? LED_WARN : LED_OFF;
    set_led(app->led_ctl, c);
    /* Auto ctl (docs/01 sec. 3.2(5)): start tone presses the record
     * button, stop tone releases it (+ auto-save when armed) */
    if (app->autoctl) {
        if (msg->state == 1)
            record_on(app);
        else if (msg->state == 2) {
            record_off(app);
            if (app->autosave)
                auto_save(app);
        }
    }
    delete msg;
}

static void tone_levels(double l300, double l450)
{
    /* runs on the worker thread (every 100 ms of video);
     * DetTime hysteresis: tone must hold for g_det_blocks blocks */
    g_streak3 = l300 > ToneDetect::LEVEL_TH ? g_streak3 + 1 : 0;
    g_streak4 = l450 > ToneDetect::LEVEL_TH ? g_streak4 + 1 : 0;
    int s = 0;
    if (g_streak3 >= g_det_blocks)
        s = 1;
    else if (g_streak4 >= g_det_blocks)
        s = 2;
    if (!g_app || s == g_tone_state)
        return;
    g_tone_state = s;
    Fl::awake(cb_tone, new ToneMsg{g_app, s});
}

static void cb_decode_done(void *p)
{
    DecodeDone *msg = (DecodeDone *)p;
    AppState *app = msg->app;
    app->decoding = false;
    g_file_decode = false;   /* audio thread resumes scope + tones */

    if (msg->img) {
        app->image = *msg->img;
        delete msg->img;
        reset_rotate(app);
        app->view->reset_zoom();   /* fresh decode: zoom back to 1/3 */
        show_image(app);
        if (!msg->error.empty())
            fl_alert("warning:\n%s", msg->error.c_str());
    } else {
        set_title(app, 0);
        fl_alert("decode failed:\n%s", msg->error.c_str());
    }
    delete msg;
}

/* settings -> SyncParams now lives in core/settings.h as
 * sync_params_from_settings(), so the tests exercise the same mapping. */

static void decode_worker(std::string path, KgSettings cfg, bool track)
{
    DecodeDone *msg = new DecodeDone;
    msg->app = g_app;
    msg->img = 0;
    try {
        SyncParams sp = sync_params_from_settings(cfg);

        std::vector<double> audio = wav_read_22050(path, 0);
        std::vector<uint8_t> video = fm_decode(audio, decode_progress,
                                               scope_block, tone_levels);
        if (cfg.rpm == 1)   /* 60 rpm: video at 4000 S/s, rest unchanged */
            video = video_halve_rate(video);
        msg->img = new FaxImage(scan_lines(video, line_state, &sp, track));
        /* lines are emitted from the start, so the image is never
         * empty; warn (but still show it) when nothing ever locked */
        if (track && msg->img->lines_locked == 0 &&
            msg->img->lines_corrected == 0) {
            msg->error = std::string("no sync pulses found - not a ") +
                         (cfg.rpm == 1 ? "60" : "120") +
                         " rpm WEFAX signal?";
        }
    } catch (const std::exception &e) {
        msg->error = e.what();
    }
    Fl::awake(cb_decode_done, msg);
}

static void start_decode(AppState *app, const char *path)
{
    app->decoding = true;
    g_file_decode = true;   /* audio thread yields scope + tones */
    g_last_led = -1;
    g_tone_state = 0;
    g_streak3 = 0;
    g_streak4 = 0;
    /* DetTime is already a COUNT of 100 ms blocks, not milliseconds:
     * the original compares it straight against its consecutive-block
     * counter (docs/01 sec. 3.2(5)). Default 20 = 2 s of tone. */
    g_det_blocks = app->settings.tone_blocks;
    if (g_det_blocks < 1)
        g_det_blocks = 1;
    set_title(app, "decoding...");
    /* settings are copied so the dialog can be edited during decode */
    std::thread(decode_worker, std::string(path), app->settings,
                app->sync_btn->value() != 0).detach();
}

static void cb_decode_wav(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    if (app->decoding || app->recording) return;
    const char *path = fl_file_chooser("Decode WAV file",
                                       "WAV files (*.wav)\t*.wav", "");
    if (!path) return;
    start_decode(app, path);
}

/* ---- Scan (live audio reception) ----
 *
 * RtAudio's callback thread runs the whole DSP chain per sample
 * (FmDecoder -> ToneDetect / LiveScan); results reach the UI only via
 * Fl::awake() messages, like the file-decode worker (DEVIATIONS #1).
 */

struct LiveState {
    explicit LiveState(const SyncParams &sp) : scan(sp) {}

    FmDecoder  dec;
    LiveScan   scan;
    ToneDetect tones;
    std::vector<uint8_t> vbuf;   /* demod output per audio block      */
    std::vector<uint8_t> hbuf;   /* 60 rpm: halved video bytes        */
    uint8_t carry;               /* unpaired byte for 60 rpm halving  */
    bool     have_carry;
    int      rpm;
    double   blk[FFT_N];         /* scope block accumulator           */
    int      bn;
    /* input level stats (dev aid, --test-scan): written on the audio
     * thread, read on the UI thread, hence atomic */
    std::atomic<double> sum_sq;
    std::atomic<long>   nsamp;
    double   lvl_sq;             /* level meter: current window       */
    long     lvl_n;
};

struct LineMsg {
    AppState *app;
    int       state;
    uint8_t   line[FaxImage::WIDTH];
};

struct LevelMsg {
    AppState *app;
    double    rms;
};

static void cb_level(void *p)
{
    LevelMsg *msg = (LevelMsg *)p;
    if (!msg->app->recording && !msg->app->decoding) {
        char buf[64];
        snprintf(buf, sizeof buf, "listening: level %.0f", msg->rms);
        set_title(msg->app, buf);
    }
    delete msg;
}

static LiveAudio g_audio;
static LiveState *g_live = 0;   /* owned by the UI thread; the audio
                                   thread only dereferences it inside
                                   callbacks, which stop() ends first */
/* Scan gate: lines -> image. Written by the UI thread, read by the
 * audio thread in on_live_line, so it is atomic - record_off() now
 * depends on the audio thread seeing it in the right order. */
static std::atomic<bool> g_recording{false};
static int  g_last_live_led = -1;  /* LED dedup while not recording   */

static void cb_live_line(void *p)
{
    LineMsg *msg = (LineMsg *)p;
    AppState *app = msg->app;
    app->image.lines.push_back(
        std::vector<uint8_t>(msg->line, msg->line + FaxImage::WIDTH));
    set_leds(app, msg->state);
    app->view->live_column(msg->line);
    char buf[64];
    snprintf(buf, sizeof buf, "receiving: %zu lines",
             app->image.lines.size());
    set_title(app, buf);
    delete msg;

    /* CycleGet (docs/01 sec. 4 "CycleGet"): when the buffer hits the
     * max width (2280 lines = the 1500x2280 image is full) and
     * auto-save is armed on the .syn filter, save + clear mid-reception
     * so capture continues into a fresh image. The original fires at
     * ArgList > 758 (the preview frontier), which is this same full-
     * buffer condition. Only the .syn branch clears, so CycleGet is a
     * no-op for .bmp (the buffer would just keep growing past 2280,
     * which we cap by dropping the oldest line, matching the
     * original's clamp at 2279). */
    if (app->settings.cycleget && app->autosave &&
        app->autosave_fmt == 0 &&
        app->image.lines.size() >= (size_t)FaxImage::MAX_LINES) {
        auto_save(app);
    } else if (app->image.lines.size() > (size_t)FaxImage::MAX_LINES) {
        /* .bmp / non-CycleGet: clamp at 2280 like the original's
         * dword_4F25BC > 2279 clamp (6916). */
        app->image.lines.erase(app->image.lines.begin());
    }
}

static void on_live_line(const uint8_t *line, int state, void *)
{
    /* runs on the audio thread */
    if (!g_app)
        return;
    if (!g_recording) {
        /* just listening: LEDs still follow the sync state */
        if (state != g_last_live_led) {
            g_last_live_led = state;
            Fl::awake(cb_led, new LedMsg{g_app, state});
        }
        return;
    }
    LineMsg *msg = new LineMsg;
    msg->app = g_app;
    msg->state = state;
    memcpy(msg->line, line, FaxImage::WIDTH);
    Fl::awake(cb_live_line, msg);
}

static void live_sample(double s, void *ud)
{
    /* runs on the audio thread, once per 22050 Hz sample */
    LiveState *ls = (LiveState *)ud;
    /* single writer, so a load+store is race-free (C++17 atomic<double>
     * has no +=) */
    ls->sum_sq.store(ls->sum_sq.load(std::memory_order_relaxed) + s * s,
                     std::memory_order_relaxed);
    ls->nsamp++;
    ls->lvl_sq += s * s;
    ls->lvl_n++;
    if (ls->lvl_n >= 44100) {   /* level meter: report every ~2 s */
        if (g_app)
            Fl::awake(cb_level,
                      new LevelMsg{g_app, sqrt(ls->lvl_sq / ls->lvl_n)});
        ls->lvl_sq = 0;
        ls->lvl_n = 0;
    }
    ls->dec.feed(s, ls->vbuf);

    ls->blk[ls->bn++] = ls->dec.bpf_out;
    if (ls->bn == FFT_N) {
        ls->bn = 0;
        if (!g_file_decode)   /* the decode worker owns the scope now */
            scope_block(ls->blk);
    }

    /* tone levels on the 8000 S/s video (before 60 rpm halving) */
    if (!g_file_decode) {   /* the decode worker owns tone_levels now */
        double l300, l450;
        for (uint8_t b : ls->vbuf)
            if (ls->tones.feed(b, l300, l450))
                tone_levels(l300, l450);
    }

    const std::vector<uint8_t> *scan_in = &ls->vbuf;
    if (ls->rpm == 1) {
        /* 60 rpm: halve to 4000 S/s with a 1-byte carry across blocks */
        ls->hbuf.clear();
        size_t i = 0;
        if (ls->have_carry && !ls->vbuf.empty()) {
            ls->hbuf.push_back((uint8_t)((ls->carry + ls->vbuf[0] + 1) / 2));
            i = 1;
            ls->have_carry = false;
        }
        for (; i + 1 < ls->vbuf.size(); i += 2)
            ls->hbuf.push_back(
                (uint8_t)((ls->vbuf[i] + ls->vbuf[i + 1] + 1) / 2));
        if (i < ls->vbuf.size()) {
            ls->carry = ls->vbuf[i];
            ls->have_carry = true;
        }
        scan_in = &ls->hbuf;
    }
    if (!scan_in->empty())
        ls->scan.feed(scan_in->data(), scan_in->size(), on_live_line, 0);
    ls->vbuf.clear();
}

/* open the sound card and run the DSP (scope/LEDs/tone detection);
 * like the original, audio runs from program start, not just while
 * recording (docs/01 sec. 3.1). Idempotent. */
static bool start_audio(AppState *app)
{
    if (app->scanning)
        return true;

    /* device: settings.wavedev (0 = default input, else list index) */
    std::vector<AudioDeviceInfo> devs = audio_input_devices();
    if (devs.empty()) {
        fl_alert("no audio input devices found");
        return false;
    }
    unsigned int dev_id = devs[0].id;
    if (app->settings.wavedev == 0) {
        for (const AudioDeviceInfo &d : devs)
            if (d.is_default) {
                dev_id = d.id;
                break;
            }
    } else if (app->settings.wavedev - 1 < (int)devs.size()) {
        dev_id = devs[app->settings.wavedev - 1].id;
    }   /* else: saved device vanished -> fall back to the first */

    LiveState *ls = new LiveState(sync_params_from_settings(app->settings));
    ls->scan.set_track(app->sync_btn->value() != 0);
    ls->rpm = app->settings.rpm;
    ls->have_carry = false;
    ls->bn = 0;
    ls->sum_sq = 0;
    ls->nsamp = 0;
    ls->lvl_sq = 0;
    ls->lvl_n = 0;

    g_last_led = -1;
    g_last_live_led = -1;
    g_tone_state = 0;
    g_streak3 = 0;
    g_streak4 = 0;
    g_det_blocks = app->settings.tone_blocks;
    if (g_det_blocks < 1)
        g_det_blocks = 1;

    std::string err;
    if (!g_audio.start(dev_id, live_sample, ls, &err)) {
        /* the saved device failed (e.g. the index is stale because the
         * device list changed since it was saved): retry once with the
         * default input before giving up */
        unsigned int def_id = devs[0].id;
        for (const AudioDeviceInfo &d : devs)
            if (d.is_default) {
                def_id = d.id;
                break;
            }
        if (def_id == dev_id ||
            !g_audio.start(def_id, live_sample, ls, &err)) {
            fl_alert("cannot open audio input:\n%s", err.c_str());
            delete ls;
            return false;
        }
    }
    g_live = ls;
    app->scanning = true;
    set_title(app, "listening...");
    return true;
}

/* Scan on: lines start going into a fresh image (the original's
 * reception button clears the buffer, docs/01 sec. 5 Button2) */
static void record_on(AppState *app)
{
    if (app->recording || app->decoding)
        return;
    if (!start_audio(app)) {
        app->scan_btn->value(0);
        return;
    }
    app->image = FaxImage();
    reset_rotate(app);
    app->view->reset_zoom();   /* reception buffer clear resets zoom */
    app->view->live_begin();
    app->scope->clear();
    g_recording = true;
    app->recording = true;
    app->scan_btn->value(1);
    set_title(app, "receiving...");
}

static void record_off(AppState *app)
{
    if (!app->recording)
        return;
    app->recording = false;   /* re-entrancy guard: set before waiting */

    /* sync_step_lock holds the newest completed line back for one line
     * of lookahead (core/live.h), so at this instant one line is still
     * inside LiveScan. Ask the audio thread to flush it and give it a
     * moment before closing the gate below, or the reception loses its
     * last line. Fl::wait also drains the line queue, so the flushed
     * line reaches the image on the way through. Bounded: if the audio
     * thread is not running (device gone) this costs 200 ms and gives
     * up. All callers of record_off are on the UI thread. */
    if (g_live) {
        g_live->scan.request_finish();
        for (int i = 0; i < 40 && !g_live->scan.finish_done(); i++)
            Fl::wait(0.005);
    }
    g_recording = false;
    app->scan_btn->value(0);
    if (g_live) {
        app->image.lines_locked     = g_live->scan.lines_locked;
        app->image.lines_corrected  = g_live->scan.lines_corrected;
        app->image.lines_coasted    = g_live->scan.lines_coasted;
        app->image.relocks          = g_live->scan.relocks;
    }
    app->view->live_end(app->image);
    char buf[96];
    snprintf(buf, sizeof buf, "%zu lines", app->image.lines.size());
    set_title(app, buf);
}

/* Auto-save (sub_40C858, docs/01 sec. 4 "Auto-save"): the output format
 * comes from Form8's filter (.syn default / .bmp). Either is named
 * DirName/YYYYMMDDHHMM.<ext>. The .syn branch CLEARS the buffer
 * afterward (matching the original); .bmp does not (also matches). */
static void auto_save(AppState *app)
{
    if (app->image.lines.empty())
        return;
    time_t t = time(0);
    struct tm lt;
    if (!local_time(&t, &lt))
        return;
    char stem[32];
    strftime(stem, sizeof stem, "%Y%m%d%H%M", &lt);
    std::string dir = app->settings.dirname;
    if (dir.empty())
        dir = exe_dir();   /* the original has no fallback (DirName +
                            * "\" + name, landing at the drive root on
                            * Windows); next-to-exe is the sane macOS
                            * equivalent (DEVIATIONS.md #10) */

    if (app->autosave_fmt == 1) {
        /* .bmp: box-average render at the Form8 size scale (docs/01
         * sec. 4, switch at 12054: case 1=3x3, 2=2x2, 3=1:1). The
         * original renders sideways (w=line count, h=1500); we render
         * upright like the manual Save-image BMP (consistent with
         * DEVIATIONS #14). No buffer clear on this branch. */
        static const int scale_of[3] = { 3, 2, 1 };
        std::vector<uint8_t> rgb;
        int w, h;
        {   /* Form2 progress dialog around the bmp render */
            ProgressScope ps(app->win->x(), app->win->y(),
                         app->win->w(), app->win->h());
            render_export_rgb(app->image, scale_of[app->autosave_size],
                              app->pal, rgb, &w, &h);
            ps.update(1.0);
        }
        std::string path = dir + "/" + stem + ".bmp";
        try {
            bmp_write(path, rgb.data(), w, h);
            char buf[160];
            snprintf(buf, sizeof buf, "auto-saved %s.bmp", stem);
            set_title(app, buf);
        } catch (const std::exception &e) {
            fl_alert("auto-save failed:\n%s", e.what());
        }
        return;
    }

    /* .syn (default): SynFax2 + mode + invert + radix-255 line count. */
    std::string path = dir + "/" + stem + ".syn";
    try {
        syn_write(path, app->image, (uint8_t)app->colormode,
                  (uint8_t)app->invert);
        char buf[160];
        snprintf(buf, sizeof buf, "auto-saved %s.syn", stem);
        set_title(app, buf);
    } catch (const std::exception &e) {
        fl_alert("auto-save failed:\n%s", e.what());
        return;   /* keep the buffer if the save failed */
    }
    /* Clear the buffer + reset stats, like the original's syn branch
     * (11965-11999). During reception (CycleGet, or the stop-tone path
     * while still recording) this starts a fresh image. */
    app->image = FaxImage();
    app->backup = FaxImage();
    app->rotated90 = false;
    if (app->vertical_btn)
        app->vertical_btn->label("Vertical");
    app->view->reset_zoom();
    app->view->live_begin();
}

static void cb_scan(Fl_Widget *w, void *ud)
{
    AppState *app = (AppState *)ud;
    if (((Fl_Toggle_Button *)w)->value())
        record_on(app);
    else
        record_off(app);
}

static void cb_autoctl(Fl_Widget *w, void *ud)
{
    AppState *app = (AppState *)ud;
    app->autoctl = ((Fl_Toggle_Button *)w)->value() != 0;
    /* tones can only be heard while listening; if audio will not start,
     * pop the button back up like record_on does for Scan */
    if (app->autoctl && !start_audio(app)) {
        app->autoctl = false;
        ((Fl_Toggle_Button *)w)->value(0);
    }
}

/* Auto save (SpeedButton3, docs/01 sec. 5): pressing the button DOWN
 * opens Form8; OK commits the settings + keeps the button down + arms
 * auto-save; Cancel pops it back up and disarms. Releasing the button
 * just disarms - no dialog. */
static void cb_autosave(Fl_Widget *w, void *ud)
{
    AppState *app = (AppState *)ud;
    Fl_Toggle_Button *b = (Fl_Toggle_Button *)w;
    if (!b->value()) {
        app->autosave = false;
        return;
    }
    std::string dir = app->settings.dirname;
    int cycleget = app->settings.cycleget ? 1 : 0;
    int fmt = app->autosave_fmt;
    int size = app->autosave_size;
    if (autosave_dialog_run(&dir, &cycleget, &fmt, &size)) {
        app->settings.dirname = dir;   /* persisted on quit */
        app->settings.cycleget = cycleget != 0;
        app->autosave_fmt = fmt;       /* runtime only, no ini key */
        app->autosave_size = size;     /* runtime only, no ini key */
        app->autosave = true;
    } else {
        b->value(0);
        app->autosave = false;
    }
}

/* Manual sync align (docs/01 sec. 3.2 "Sync-track enable" + sec. 4
 * "Zoom/pan"; the original readme's 同期処理の停止と手動同期位置指定).
 * When the signal is buried in noise, or carries something that merely
 * looks like a sync pulse (photographs, mostly), automatic tracking
 * makes the picture worse - so you release the Sync button to freeze
 * the phase, then click on the preview where the sync signal really
 * is and tracking resumes from there. Click just BELOW the sync strip:
 * that is where the line starts.
 *
 * The preview's 500 rows are one line's 4000 samples with the line
 * start at the BOTTOM (faxview.cpp live_column), so a release at row y
 * sits 8*(500 - y) samples along the line from the current start -
 * the original's own formula. Nudging by that amount puts the clicked
 * position at the start of the line. */
static void cb_live_click(int y, void *ud)
{
    AppState *app = (AppState *)ud;
    if (!g_live || !app->recording)
        return;
    if (app->sync_btn->value() != 0)
        return;        /* only while tracking is OFF, like the original */
    g_live->scan.nudge_phase(8L * (500 - y));
    app->sync_btn->value(1);   /* the original presses the button too;
                                  nudge_phase already re-enabled
                                  tracking, so don't call cb_sync */
}

/* Sync toggle (docs/01 sec. 3.2 "Sync-track enable"): with tracking
 * OFF the decoder freezes the sync phase and both sync LEDs go black;
 * turning it back ON re-acquires lock through the normal warmup. */
static void cb_sync(Fl_Widget *w, void *ud)
{
    AppState *app = (AppState *)ud;
    bool on = ((Fl_Toggle_Button *)w)->value() != 0;
    if (g_live)
        g_live->scan.set_track(on);
    if (!on)
        set_leds(app, 3);
}

/* ---- input device chooser (Form9) ---- */

static void cb_input_device(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    int sel = device_dialog_run(app->settings.wavedev);
    if (sel < 0 || sel == app->settings.wavedev)
        return;
    app->settings.wavedev = sel;   /* saved on quit */
    if (!app->scanning)
        return;
    /* switch the running capture to the newly selected device, showing
     * the Form10 notice while the stream is torn down + reopened */
    show_notice(app->win->x(), app->win->y(),
                app->win->w(), app->win->h());
    g_audio.stop();      /* pauses the stream; callback is done after */
    delete g_live;
    g_live = 0;
    app->scanning = false;
    start_audio(app);
    hide_notice();
}

/* ---- Quit ---- */

/* persist choices + window position, write the ini (docs/01 sec. 6) */
static void save_settings(AppState *app)
{
    app->settings.rpm   = app->rpm_choice->value();
    app->settings.syn   = app->syn_choice->value();
    app->settings.formx = app->win->x();
    app->settings.formy = app->win->y();
    try {
        settings_write(settings_path(), app->settings);
    } catch (const std::exception &e) {
        fprintf(stderr, "cannot save settings: %s\n", e.what());
    }
}

static void cb_quit(Fl_Widget *, void *ud)
{
    AppState *app = (AppState *)ud;
    record_off(app);
    g_audio.close();   /* end audio callbacks before widgets/settings go */
    save_settings(app);
    app->win->hide();   /* last window hidden -> Fl::run() returns */
}

/* ---- UI construction: coordinates from docs/05-gui-layout.md Form1.
 * GOTCHA: plain Fl_Group does NOT offset its children - child x/y are
 * window coordinates. So the VCL parent-relative Left/Top values must
 * be added to the group origin (see RX/PAR/LP offsets below).
 * Fl_Scroll is no exception in FLTK 1.4: its children are also window
 * coordinates (scroll_to() physically moves them), so the FaxView sits
 * at the scroll's inner-box origin, not (0,0). At (0,0) the child
 * bounding box pokes above/left of the frame and FLTK shows both
 * scrollbars permanently. */

/* one LED row inside Panel1: text label at left, 17x9 LED at x=72;
 * returns the LED box so decode state can recolor it */
static Fl_Box *add_led(int px, int py, int y, const char *text)
{
    Fl_Box *lbl = new Fl_Box(px + 16, py + y - 2, 48, 12, text);
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lbl->labelsize(10);
    Fl_Box *led = new Fl_Box(px + 72, py + y, 17, 9);
    led->box(FL_FLAT_BOX);
    led->color(LED_OFF);
    return led;
}

static void build_ui(AppState *app)
{
    /* ScrollBox1 (5,8,764,504) with Image1 (760x500) inside */
    Fl_Scroll *scroll = new Fl_Scroll(5, 8, 764, 504);
    scroll->box(FL_DOWN_BOX);
    app->view = new FaxView(scroll->x() + Fl::box_dx(scroll->box()),
                            scroll->y() + Fl::box_dy(scroll->box()),
                            760, 500);
    app->view->set_live_click_cb(cb_live_click, app);
    scroll->end();

    /* Panel7 (776,8,102,108) holding the scope (was Image3) */
    Fl_Box *spec = new Fl_Box(776, 8, 102, 108);
    spec->box(FL_BORDER_BOX);
    app->scope = new ScopeView(777, 9, 100, 106);

    /* GroupBox2 Reception (775,122,105,193): 4 speed buttons 89x33 */
    const int RX = 775, RY = 122;
    Fl_Group *rx = new Fl_Group(RX, RY, 105, 193, "Reception");
    rx->box(FL_ENGRAVED_BOX);
    rx->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    rx->labelsize(10);
    Fl_Toggle_Button *t;
    t = new Fl_Toggle_Button(RX + 8, RY + 24, 89, 33, "Scan");
    t->callback(cb_scan, app);
    app->scan_btn = t;
    t = new Fl_Toggle_Button(RX + 8, RY + 64, 89, 33, "Auto ctl");
    t->callback(cb_autoctl, app);
    app->autoctl_btn = t;
    t = new Fl_Toggle_Button(RX + 8, RY + 104, 89, 33, "Auto save");
    t->callback(cb_autosave, app);
    app->autosave_btn = t;
    t = new Fl_Toggle_Button(RX + 8, RY + 144, 89, 33, "Sync");
    t->callback(cb_sync, app);
    t->value(1);   /* default ON (docs/01 sec. 3.2: not persisted) */
    app->sync_btn = t;
    rx->end();

    /* GroupBox1 Parameters (775,321,105,129) */
    const int PX = 775, PY = 321;
    Fl_Group *par = new Fl_Group(PX, PY, 105, 129, "Parameters");
    par->box(FL_ENGRAVED_BOX);
    par->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    par->labelsize(10);
    Fl_Box *lbl = new Fl_Box(PX + 11, PY + 16, 48, 12, "Speed");
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lbl->labelsize(10);
    app->rpm_choice = new Fl_Choice(PX + 24, PY + 30, 73, 20);
    app->rpm_choice->textsize(11);
    app->rpm_choice->add("120 rpm");
    app->rpm_choice->add("60 rpm");
    app->rpm_choice->value(0);
    app->rpm_choice->callback(cb_choice, app);
    lbl = new Fl_Box(PX + 11, PY + 53, 60, 12, "Sync len");
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lbl->labelsize(10);
    app->syn_choice = new Fl_Choice(PX + 24, PY + 68, 73, 20);
    app->syn_choice->textsize(11);
    /* NOTE: docs/05 has no item list for ComboBox4 (empty in the DFM
     * dump). Values follow docs/01 sec. 5 (5*(i+1) ms) and the
     * reference screenshot, which shows "20 msec" selected. */
    app->syn_choice->add("5 msec");
    app->syn_choice->add("10 msec");
    app->syn_choice->add("15 msec");
    app->syn_choice->add("20 msec");
    app->syn_choice->add("25 msec");
    app->syn_choice->add("30 msec");
    app->syn_choice->add("35 msec");
    app->syn_choice->add("40 msec");
    app->syn_choice->add("45 msec");
    app->syn_choice->add("50 msec");
    app->syn_choice->value(3);   /* "20 msec", as in the screenshot */
    app->syn_choice->callback(cb_choice, app);
    Fl_Button *dev = new Fl_Button(PX + 8, PY + 94, 25, 25, "@menu");
    dev->tooltip("Input device…");
    dev->callback(cb_input_device, app);
    Fl_Button *cfg = new Fl_Button(PX + 39, PY + 94, 60, 25, "Details…");
    cfg->labelsize(11);
    cfg->callback(cb_details, app);
    par->end();

    /* Panel1 (776,456,105,57): three status LEDs */
    const int LP = 776, LT = 456;
    Fl_Group *leds = new Fl_Group(LP, LT, 105, 57);
    leds->box(FL_BORDER_BOX);
    app->led_sync = add_led(LP, LT, 8,  "Sync det");
    app->led_corr = add_led(LP, LT, 24, "Sync corr");
    app->led_ctl  = add_led(LP, LT, 40, "Control");
    leds->end();

    /* bottom button row, y=520, 90x25 (BitBtn1 105x25) */
    Fl_Menu_Button *open = new Fl_Menu_Button(8, 520, 90, 25, "Load data");
    open->add("Load data (.syn)...", 0, cb_open_syn, app);
    open->add("[dev] Decode WAV...", 0, cb_decode_wav, app);
    open->label("Load data");   /* keep original caption on the button */
    Fl_Button *b;
    b = new Fl_Button(104, 520, 90, 25, "Save data");
    b->callback(cb_save_syn, app);
    b = new Fl_Button(200, 520, 90, 25, "Save image");
    b->callback(cb_save_image, app);
    b = new Fl_Button(296, 520, 90, 25, "Print");
    b->callback(cb_print, app);
    b = new Fl_Button(392, 520, 90, 25, "Clear");
    b->callback(cb_clear, app);
    b = new Fl_Button(488, 520, 90, 25, "Vertical");
    b->callback(cb_vertical, app);
    app->vertical_btn = b;
    b = new Fl_Button(584, 520, 90, 25, "XY flip");
    b->callback(cb_xyflip, app);
    b = new Fl_Button(680, 520, 90, 25, "Color");
    b->callback(cb_color, app);
    b = new Fl_Button(776, 520, 105, 25, "Quit");
    b->callback(cb_quit, app);
}

/* clamp helper for applying loaded settings to combo indices */
static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --test-progress demo: drive a Form2 progress bar 0 -> 1 over ~1 s.
 * (dev aid only; the real renders wrap render_export_rgb in a
 * ProgressScope instead.) */
static void animate_progress_demo(int parent_x, int parent_y,
                                  int parent_w, int parent_h)
{
    ProgressScope *ps = new ProgressScope(parent_x, parent_y,
                                          parent_w, parent_h);
    for (int i = 0; i <= 20; i++) {
        ps->update(i / 20.0);
        Fl::wait(0.05);   /* ~50 ms per step -> ~1 s total */
    }
    delete ps;
}

int main(int argc, char **argv)
{
    Fl::lock();   /* required so worker threads may call Fl::awake() */

    AppState app;
    app.decoding = false;
    app.scanning = false;
    app.recording = false;
    app.autoctl = false;
    app.autosave = false;
    app.autosave_fmt = 0;    /* Form8: .syn (the original's default filter) */
    app.autosave_size = 0;   /* Form8: 760x500 */
    app.colormode = 0;      /* palette defaults: monotone, positive */
    app.invert = 0;
    app.export_kind = 0;
    app.export_portrait = 0;
    app.rotated90 = false;
    app.vertical_btn = 0;   /* set in build_ui */
    g_app = &app;

    /* settings: defaults, then isobar.ini (next to the exe), or a one-time
     * import of the original's kgfax.ini if that is all there is */
    settings_load(app.settings);

    if (argc > 1 && strcmp(argv[1], "--dump-settings") == 0) {
        settings_write_stream(std::cout, app.settings);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--list-devices") == 0) {
        /* DEV AID: print audio input devices (Form9 list), exit */
        std::vector<AudioDeviceInfo> devs = audio_input_devices();
        printf("0: Default input device\n");
        for (size_t i = 0; i < devs.size(); i++)
            printf("%zu: %s (%u ch)%s\n", i + 1, devs[i].name.c_str(),
                   devs[i].in_channels,
                   devs[i].is_default ? " [default]" : "");
        if (devs.empty())
            printf("(no input devices found)\n");
        return 0;
    }

    Fl_Double_Window *win = new Fl_Double_Window(888, 551, "Isobar");
    /* restore saved position, clamped so it can't land off-screen */
    win->position(clampi(app.settings.formx, 0, Fl::w() - 100),
                  clampi(app.settings.formy, 0, Fl::h() - 100));
    win->callback(cb_quit, &app);   /* title-bar close = Quit */
    app.win = win;
    build_ui(&app);
    apply_palette(&app);   /* default monotone palette into the view */
    win->end();

    /* restore persisted main-form choices */
    app.rpm_choice->value(clampi(app.settings.rpm, 0, 1));
    app.syn_choice->value(clampi(app.settings.syn, 0, 9));

    /* Window/taskbar icon. On macOS this is a no-op (the .app bundle's
     * CFBundleIconFile supplies the dock/Finder icon); on Linux/X11 and
     * Windows this sets the taskbar icon. resource_dir() finds the PNG
     * whether it sits next to the exe (flat) or in Resources/ (.app).
     * Missing file is non-fatal — the window just gets the default icon. */
    {
        std::string icon_path = resource_dir() + "/isobar-128.png";
        Fl_PNG_Image *icon = new Fl_PNG_Image(icon_path.c_str());
        if (icon->w() > 0 && icon->h() > 0)
            win->icon(icon);
        else
            delete icon;
    }

    win->show();   /* no argc/argv: our argv[1] is a .syn path, and
                      FLTK would otherwise misparse it as an X option */

    if (argc > 1 && strcmp(argv[1], "--test-scope") == 0) {
        /* DEV AID: start a WAV decode immediately (exercises the scope
         * and LED paths without clicking). */
        start_decode(&app, argc > 2 ? argv[2] : "jmh sample.wav");
    } else if (argc > 1 && strcmp(argv[1], "--test-details") == 0) {
        /* DEV AID: open the Details dialog immediately. */
        settings_dialog_run(app.settings);
    } else if (argc > 1 && strcmp(argv[1], "--test-color") == 0) {
        /* DEV AID: decode the sample, then open the Color dialog and
         * apply the result (exercises dialog + re-render). */
        start_decode(&app, argc > 2 ? argv[2] : "jmh sample.wav");
        while (app.decoding)
            Fl::wait();
        cb_color(0, &app);
    } else if (argc > 1 && strcmp(argv[1], "--test-palette") == 0) {
        /* DEV AID: --test-palette <mode> [invert] [file.wav]: decode
         * with the given palette pre-applied (visual palette check). */
        app.colormode = argc > 2 ? atoi(argv[2]) : 0;
        app.invert = argc > 3 ? atoi(argv[3]) : 0;
        apply_palette(&app);
        start_decode(&app, argc > 4 ? argv[4] : "jmh sample.wav");
    } else if (argc > 1 && strcmp(argv[1], "--test-export") == 0) {
        /* DEV AID: --test-export <kind> <portrait> <mode> [invert]:
         * decode the sample, then run the Save-image render path to
         * /tmp/isobar-export.bmp (no dialogs, exercises render + rotate
         * + BMP write). */
        int kind = argc > 2 ? atoi(argv[2]) : 3;
        int portrait = argc > 3 ? atoi(argv[3]) : 0;
        app.colormode = argc > 4 ? atoi(argv[4]) : 0;
        app.invert = argc > 5 ? atoi(argv[5]) : 0;
        apply_palette(&app);
        app.export_kind = kind;
        app.export_portrait = portrait;
        start_decode(&app, "jmh sample.wav");
        while (app.decoding)
            Fl::wait();
        static const int scale_of[4] = { 1, 3, 2, 1 };
        std::vector<uint8_t> rgb;
        int w, h;
        render_export_rgb(app.image, scale_of[kind], app.pal, rgb, &w, &h);
        /* rotate when "Land." selected (see cb_save_image) */
        if (!portrait)
            rotate90ccw(rgb, &w, &h);
        bmp_write("/tmp/isobar-export.bmp", rgb.data(), w, h);
        fprintf(stderr, "export: kind %d portrait %d mode %d -> %dx%d\n",
                kind, portrait, app.colormode, w, h);
    } else if (argc > 1 && strcmp(argv[1], "--test-print") == 0) {
        /* DEV AID: render the exact print bitmap (raster, palette,
         * full buffer, pre-rotate) to /tmp/print-test.bmp and exit -
         * no Fl_Printer (can't click the native panel headlessly). */
        try {
            load_syn(&app, argc > 2 ? argv[2] : "out.syn");
        } catch (const std::exception &e) {
            fl_alert("cannot load:\n%s", e.what());
        }
        std::vector<uint8_t> rgb;
        int w, h;
        render_print_rgb(app.image, app.pal, rgb, &w, &h);
        bmp_write("/tmp/print-test.bmp", rgb.data(), w, h);
        printf("/tmp/print-test.bmp (%dx%d)\n", w, h);
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "--test-autosave-dialog") == 0) {
        /* DEV AID: open the Form8 auto-save dialog over the main
         * window (out.syn loaded); stays up for screenshots. */
        try {
            load_syn(&app, argc > 2 ? argv[2] : "out.syn");
        } catch (const std::exception &e) {
            fl_alert("cannot load:\n%s", e.what());
        }
        app.autosave_btn->value(1);
        cb_autosave(app.autosave_btn, &app);
    } else if (argc > 1 && strcmp(argv[1], "--test-zoom") == 0) {
        /* DEV AID: --test-zoom N [file.syn]: load the .syn, zoom the
         * preview to level N (1 or 2) as if center-clicked at
         * (380,250), leave the window open for screenshots. */
        int level = argc > 2 ? atoi(argv[2]) : 1;
        try {
            load_syn(&app, argc > 3 ? argv[3] : "out.syn");
        } catch (const std::exception &e) {
            fl_alert("cannot load:\n%s", e.what());
        }
        app.view->dev_zoom_to(level);
    } else if (argc > 1 &&
               (strcmp(argv[1], "--test-rotate90") == 0 ||
                strcmp(argv[1], "--test-rotate180") == 0)) {
        /* DEV AID: load out.syn (or the given .syn), apply the
         * Vertical (90-degree rotate) or XY-flip (180-degree) button
         * operation once, leave the window open for screenshots. */
        try {
            load_syn(&app, argc > 2 ? argv[2] : "out.syn");
        } catch (const std::exception &e) {
            fl_alert("cannot load:\n%s", e.what());
        }
        if (strcmp(argv[1], "--test-rotate90") == 0)
            cb_vertical(0, &app);
        else
            cb_xyflip(0, &app);
    } else if (argc > 1 && strcmp(argv[1], "--test-notice") == 0) {
        /* DEV AID: pop the Form10 recording-notice for 2 s (visual
         * check of the device-switch popup), then dismiss it. */
        Fl::check();
        show_notice(app.win->x(), app.win->y(),
                    app.win->w(), app.win->h());
        Fl::add_timeout(2.0, [](void *) { hide_notice(); }, 0);
    } else if (argc > 1 && strcmp(argv[1], "--test-progress") == 0) {
        /* DEV AID: animate the Form2 progress bar 0 -> 1 over ~1 s
         * (visual check of the render progress dialog). */
        Fl::check();
        animate_progress_demo(app.win->x(), app.win->y(),
                              app.win->w(), app.win->h());
    } else if (argc > 1 && strcmp(argv[1], "--test-autosave") == 0) {
        /* DEV AID: --test-autosave [file.syn] [fmt] [size]: load the
         * .syn, arm auto-save (CycleGet) pointed at /tmp, fire
         * auto_save() once, report whether the file was written + the
         * buffer state. fmt 0=.syn (default) / 1=.bmp; size 0..2 is the
         * bmp render scale. */
        try {
            load_syn(&app, argc > 2 ? argv[2] : "out.syn");
        } catch (const std::exception &e) {
            fl_alert("cannot load:\n%s", e.what());
        }
        app.settings.dirname = "/tmp";
        app.autosave = true;
        app.autosave_fmt = argc > 3 ? atoi(argv[3]) : 0;
        app.autosave_size = argc > 4 ? atoi(argv[4]) : 0;
        app.settings.cycleget = 1;
        size_t before = app.image.lines.size();
        auto_save(&app);
        fprintf(stderr, "test-autosave: fmt=%d size=%d, had %zu lines, "
                "now %zu lines; check /tmp for a YYYYMMDDHHMM.%s\n",
                app.autosave_fmt, app.autosave_size, before,
                app.image.lines.size(),
                app.autosave_fmt == 1 ? "bmp" : "syn");
    } else if (argc > 1 && strcmp(argv[1], "--test-quit-save") == 0) {
        /* DEV AID: run the Quit path right away (tests the ini write
         * without clicking; window hides -> Fl::run() returns). */
        Fl::check();   /* let the window map so x/y are real */
        cb_quit(0, &app);
    } else if (argc > 1 && strcmp(argv[1], "--test-scan") == 0) {
        /* DEV AID: listen for N seconds, then quit. Optional args:
         * device list index (see --list-devices) and seconds; a 4th
         * arg "auto" arms Auto ctl instead of recording manually
         * (loopback auto start/stop test via playwav + BlackHole). */
        if (argc > 2)
            app.settings.wavedev = atoi(argv[2]);
        double secs = argc > 3 ? atof(argv[3]) : 5.0;
        bool auto_mode = argc > 4 && strcmp(argv[4], "auto") == 0;
        if (auto_mode) {
            app.autoctl = true;
            app.autoctl_btn->value(1);
            app.autosave = true;   /* exercises the stop-tone save too */
            app.autosave_btn->value(1);
            app.settings.dirname = "/tmp";   /* keep test files out of ~ */
            start_audio(&app);
        } else {
            record_on(&app);
        }
        Fl::add_timeout(secs, [](void *ud) {
            AppState *a = (AppState *)ud;
            double rms = (g_live && g_live->nsamp)
                ? sqrt(g_live->sum_sq / g_live->nsamp) : 0.0;
            fprintf(stderr, "test-scan: %s, recording %s, %zu lines, "
                    "input rms %.0f\n",
                    a->scanning ? "capture ran" : "capture FAILED to start",
                    a->recording ? "on" : "off",
                    a->image.lines.size(), rms);
            /* keep the received image for inspection */
            std::ofstream pgm("/tmp/live-test.pgm", std::ios::binary);
            if (pgm) {
                pgm << "P5\n" << FaxImage::WIDTH << " "
                    << a->image.lines.size() << "\n255\n";
                for (auto &ln : a->image.lines)
                    pgm.write((const char *)ln.data(),
                              (std::streamsize)ln.size());
            }
            cb_quit(0, a);
        }, &app);
    } else {
        /* normal startup: audio runs from the start, like the
         * original (docs/01 sec. 3.1) */
        start_audio(&app);
        if (argc > 1) {
            try {
                load_syn(&app, argv[1]);
            } catch (const std::exception &e) {
                fl_alert("cannot load %s:\n%s", argv[1], e.what());
            }
        }
    }

    return Fl::run();
}
