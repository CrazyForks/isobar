/* settingsdialog.cpp - see settingsdialog.h.
 *
 * Control -> setting mapping (docs/01-program-analysis.md sec. 5 lists
 * the edited fields; the docs do not name each label, so rows are
 * matched by the Form4 y-positions in docs/05):
 *   y=16  ComboBox1   -> SyncThre   (value = 20*index+10)
 *   y=40  Panel1/UpDown1   -> LReSycn
 *   y=59  Panel4/UpDown4   -> RReSycn
 *   y=78  Panel5/UpDown5   -> SyncWidth (n, width = 10*n ms)
 *   y=97  Panel2/UpDown2   -> Sync2Thre
 *   y=116 Panel3/UpDown3   -> DetTime
 * The original's Panel+UpDown pairs become Fl_Spinners.
 *
 * COORDINATE GOTCHA (same as the main window): plain Fl_Group does not
 * offset its children, so group-relative VCL coordinates are added to
 * the group origin (GX/GY, IX/IY below).
 */
#include "settingsdialog.h"

#include "isobar_version.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Spinner.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/fl_ask.H>

namespace {

struct Dlg {
    bool        ok;
    KgSettings *work;     /* edited copy, committed only on OK */
    Fl_Choice  *th;       /* SyncThre combo */
    Fl_Spinner *lr;       /* LReSycn   */
    Fl_Spinner *rr;       /* RReSycn   */
    Fl_Spinner *sw;       /* SyncWidth */
    Fl_Spinner *s2;       /* Sync2Thre */
    Fl_Spinner *dt;       /* DetTime   */
};

void cb_ok(Fl_Widget *w, void *ud)
{
    Dlg *d = (Dlg *)ud;
    KgSettings &s = *d->work;
    s.synthre   = 20 * d->th->value() + 10;   /* docs/01 sec. 5 */
    s.lresycn   = (int)d->lr->value();
    s.rresycn   = (int)d->rr->value();
    s.syncwidth = (int)d->sw->value();
    s.sync2thre = (int)d->s2->value();
    s.dettime   = (int)d->dt->value();
    d->ok = true;
    w->window()->hide();
}

void cb_cancel(Fl_Widget *w, void *)
{
    w->window()->hide();
}

Fl_Box *add_label(int x, int y, int w, const char *text)
{
    Fl_Box *l = new Fl_Box(x, y, w, 12, text);
    l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    l->labelsize(10);
    return l;
}

Fl_Spinner *add_spinner(int x, int y, double lo, double hi, double val)
{
    Fl_Spinner *sp = new Fl_Spinner(x, y, 58, 17);
    sp->minimum(lo);
    sp->maximum(hi);
    sp->step(1);
    sp->value(val);
    sp->textsize(10);
    return sp;
}

} /* namespace */

bool settings_dialog_run(KgSettings &s)
{
    KgSettings work = s;   /* edit a copy; commit on OK only */
    Dlg d;
    d.ok = false;
    d.work = &work;

    Fl_Double_Window *dlg = new Fl_Double_Window(320, 192, "Details");
    dlg->set_modal();

    /* GroupBox3 "Sync" (8,8,193,145): sync processing settings */
    const int GX = 8, GY = 8;
    Fl_Group *g = new Fl_Group(GX, GY, 193, 145, "Sync");
    g->box(FL_ENGRAVED_BOX);
    g->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    g->labelsize(10);
    add_label(GX + 8, GY + 20, 96, "Sync detect");
    d.th = new Fl_Choice(GX + 120, GY + 16, 57, 20);
    d.th->textsize(10);
    /* The original's 3 named presets (DFM ComboBox1.Items.Strings):
     *   厳しい (Strict)  index 0 -> SyncThre 10
     *   通常   (Normal)  index 1 -> SyncThre 30
     *   優しい (Gentle)  index 2 -> SyncThre 50
     * via the traced formula SyncThre = 20*index + 10. */
    d.th->add("Strict");
    d.th->add("Normal");
    d.th->add("Gentle");
    int ti = (work.synthre - 10) / 20;      /* value -> combo index */
    if (ti < 0) ti = 0;
    if (ti > 2) ti = 2;                     /* only 3 presets */
    d.th->value(ti);
    add_label(GX + 8, GY + 45, 90, "Lock/release");
    d.lr = add_spinner(GX + 120, GY + 40, 1, 100, work.lresycn);
    add_label(GX + 8, GY + 64, 84, "Full release");
    d.rr = add_spinner(GX + 120, GY + 59, 1, 200, work.rresycn);
    add_label(GX + 8, GY + 83, 72, "Corr range");
    d.sw = add_spinner(GX + 120, GY + 78, 1, 10, work.syncwidth);
    add_label(GX + 8, GY + 102, 72, "Sync thresh");
    d.s2 = add_spinner(GX + 120, GY + 97, 0, 255, work.sync2thre);
    add_label(GX + 8, GY + 121, 96, "Ctl det time");
    d.dt = add_spinner(GX + 120, GY + 116, 0, 5000, work.dettime);
    g->end();

    /* GroupBox6 "Info" (208,8,105,145): neutral port info, NOT the
     * original's version string/branding (legal policy). */
    const int IX = 208, IY = 8;
    Fl_Group *gi = new Fl_Group(IX, IY, 105, 145, "Info");
    gi->box(FL_ENGRAVED_BOX);
    gi->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    gi->labelsize(10);
    /* App icon (the same PNG used for the window icon), scaled into the
     * box. resource_dir() finds it next to the exe or in a .app's
     * Resources/. Missing file leaves the empty bordered box — non-fatal. */
    Fl_Box *icon = new Fl_Box(IX + 36, IY + 23, 33, 33);
    icon->box(FL_BORDER_BOX);
    {
        Fl_PNG_Image *img = new Fl_PNG_Image(
            (resource_dir() + "/isobar-128.png").c_str());
        if (img->w() > 0 && img->h() > 0)
            icon->image(img->copy(33, 33));
        delete img;
    }
    /* Name+version on one line, then author/licence, then the two
     * libraries we link (their licences are in NOTICE), then the file
     * -format note. All centred on the full inner width so a longer
     * version string stays centred instead of drifting. */
    struct InfoLine { int y, size; const char *text; };
    static const InfoLine info[] = {
        { 69, 10, "Isobar " ISOBAR_VERSION },
        { 85,  9, "\xc2\xa9 Sara Sakuragawa" },
        { 99,  9, "GPL v3+" },
        { 113, 8, "uses FLTK + RtAudio" },
        { 127, 9, ".syn compatible" },
    };
    for (const InfoLine &il : info) {
        Fl_Box *t = add_label(IX + 4, IY + il.y, 98, il.text);
        t->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        t->labelsize(il.size);
    }
    gi->end();

    /* OK / Cancel: added by the port (original Form4 has none; it
     * applies on close). Placed below the groups -> window is taller. */
    Fl_Return_Button *ok = new Fl_Return_Button(86, 160, 70, 25, "OK");
    ok->callback(cb_ok, &d);
    Fl_Button *cancel = new Fl_Button(166, 160, 70, 25, "Cancel");
    cancel->callback(cb_cancel);

    dlg->end();
    dlg->show();
    while (dlg->shown())
        Fl::wait();

    bool commit = d.ok;
    if (commit) {
        s = work;
        try {
            settings_write(settings_path(), s);
        } catch (const std::exception &e) {
            fl_alert("cannot save settings:\n%s", e.what());
        }
    }
    delete dlg;   /* deletes child widgets too */
    return commit;
}
