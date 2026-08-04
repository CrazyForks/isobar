/* autosavedialog.cpp - see autosavedialog.h.
 *
 * Layout follows Form8 (docs/05-gui-layout.md): client 558x167, the
 * two right-side group boxes at their original coordinates
 * (GroupBox2 399,25,153x41; GroupBox1 399,72,153x64; OK 399,138,
 * Cancel 479,138). English captions (DEVIATIONS #5).
 * NOTE: plain Fl_Group does not offset its children, so the
 * checkbox/radios use window-absolute coordinates (original's
 * group-relative + group origin).
 *
 * The original's FilterComboBox1 (syn/bmp/jpg, at 208,141) becomes a
 * two-radio "Save format" group at the same spot (JPEG dropped -
 * DEVIATIONS #15). Picking .syn disables the size radios (the
 * original's FilterComboBox1Change disables them + the drive controls
 * for the syn filter).
 */
#include "autosavedialog.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Radio_Round_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_File_Chooser.H>

namespace {

struct Dlg {
    int ok;
    int cycleget, fmt, size;
    std::string dir;
    Fl_Output *path;
    Fl_Widget *size_widgets[4];   /* group + 3 radios, to enable/disable */
    Fl_Radio_Round_Button *fmt_widgets[2];  /* syn / bmp radios     */
};

void cb_ok(Fl_Widget *w, void *ud)
{
    Dlg *d = (Dlg *)ud;
    d->ok = 1;
    w->window()->hide();
}

void cb_cancel(Fl_Widget *w, void *)
{
    w->window()->hide();
}

void cb_browse(Fl_Widget *, void *ud)
{
    Dlg *d = (Dlg *)ud;
    const char *dir = fl_dir_chooser("Auto-save folder",
                                     d->dir.empty() ? "." : d->dir.c_str(),
                                     0);
    if (!dir)
        return;   /* cancelled: keep the old path */
    d->dir = dir;
    d->path->value(d->dir.c_str());
}

/* Like the original's FilterComboBox1Change: the size radios only
 * matter for the bmp render, so grey them out when .syn is chosen.
 * Either radio's callback reaches here; we just re-derive the choice
 * from which of the two is now selected. */
void cb_fmt(Fl_Widget *, void *ud)
{
    Dlg *d = (Dlg *)ud;
    int bmp = d->fmt_widgets[1]->value() != 0;
    for (int i = 0; i < 4; i++) {
        if (bmp)
            d->size_widgets[i]->activate();
        else
            d->size_widgets[i]->deactivate();
    }
}

}   /* namespace */

int autosave_dialog_run(std::string *dirname, int *cycleget,
                        int *fmt, int *size)
{
    Dlg d;
    d.ok = 0;
    d.cycleget = *cycleget;
    d.fmt = *fmt;
    d.size = *size;
    d.dir = *dirname;
    d.path = 0;
    for (int i = 0; i < 4; i++)
        d.size_widgets[i] = 0;
    for (int i = 0; i < 2; i++)
        d.fmt_widgets[i] = 0;

    /* Form8: client 558x167 */
    Fl_Double_Window *dlg = new Fl_Double_Window(558, 167,
                                                 "Auto-save settings");
    dlg->set_modal();

    /* left side: folder picker (native chooser replaces the original's
     * drive/dir/file listboxes, DEVIATIONS #13) */
    Fl_Box *lbl = new Fl_Box(8, 8, 136, 12, "Auto-save folder");
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lbl->labelsize(10);
    d.path = new Fl_Output(8, 24, 305, 23);
    d.path->value(d.dir.c_str());
    Fl_Button *br = new Fl_Button(320, 24, 73, 23, "Browse…");
    br->callback(cb_browse, &d);

    /* GroupBox2 (399,25,153,41): CycleGet checkbox */
    Fl_Group *cg = new Fl_Group(399, 25, 153, 41, "At max scan width");
    cg->box(FL_ENGRAVED_BOX);
    cg->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    cg->labelsize(10);
    Fl_Check_Button *cyc = new Fl_Check_Button(415, 41, 121, 17,
                                               "Restart scan");
    cyc->value(d.cycleget != 0);
    cg->end();

    /* GroupBox1 (399,72,153,64): image-size radios (bmp render only) */
    Fl_Group *sg = new Fl_Group(399, 72, 153, 64, "Saved image size");
    sg->box(FL_ENGRAVED_BOX);
    sg->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    sg->labelsize(10);
    Fl_Radio_Round_Button *r0 = new Fl_Radio_Round_Button(415, 84, 113, 17,
                                                          "760x500 Pixel");
    Fl_Radio_Round_Button *r1 = new Fl_Radio_Round_Button(415, 99, 113, 17,
                                                          "1140x750 Pixel");
    Fl_Radio_Round_Button *r2 = new Fl_Radio_Round_Button(415, 114, 113, 17,
                                                          "2280x1500 Pixel");
    sg->end();
    d.size_widgets[0] = sg;
    d.size_widgets[1] = r0;
    d.size_widgets[2] = r1;
    d.size_widgets[3] = r2;

    /* Save format group (replaces FilterComboBox1 at 208,141): two
     * radios, .syn (default) / .bmp. JPEG is not offered (DEVIATIONS
     * #15). Size radios are meaningful only for bmp. */
    Fl_Group *fg = new Fl_Group(208, 132, 185, 30, "Save format");
    fg->box(FL_ENGRAVED_BOX);
    fg->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fg->labelsize(10);
    Fl_Radio_Round_Button *fsyn = new Fl_Radio_Round_Button(214, 144, 70, 17,
                                                            ".syn");
    Fl_Radio_Round_Button *fbmp = new Fl_Radio_Round_Button(290, 144, 70, 17,
                                                            ".bmp");
    fsyn->callback(cb_fmt, &d);
    fbmp->callback(cb_fmt, &d);
    fg->end();
    d.fmt_widgets[0] = fsyn;
    d.fmt_widgets[1] = fbmp;

    Fl_Return_Button *ok = new Fl_Return_Button(399, 138, 73, 23, "OK");
    ok->callback(cb_ok, &d);
    Fl_Button *can = new Fl_Button(479, 138, 73, 23, "Cancel");
    can->callback(cb_cancel);

    dlg->end();

    if (d.fmt != 0 && d.fmt != 1)
        d.fmt = 0;
    if (d.size < 0 || d.size > 2)
        d.size = 0;
    (d.fmt == 1 ? fbmp : fsyn)->setonly();
    (d.size == 2 ? r2 : (d.size == 1 ? r1 : r0))->setonly();
    cb_fmt(fsyn, &d);   /* apply initial syn/bmp enable state */

    dlg->show();
    while (dlg->shown())
        Fl::wait();

    int accepted = d.ok;
    if (accepted) {
        *dirname = d.dir;
        *cycleget = cyc->value() != 0;
        *fmt = fbmp->value() ? 1 : 0;
        *size = r2->value() ? 2 : (r1->value() ? 1 : 0);
    }
    delete dlg;
    return accepted;
}
