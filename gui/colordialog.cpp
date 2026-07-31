/* colordialog.cpp - see colordialog.h.
 *
 * Layout follows Form5 (docs/05-gui-layout.md): 280x111 client, sample
 * strip at (32,24,25,81), radios in the "Type" group at (80,8,121,73),
 * Invert at (88,88), OK/Cancel at x=208. English captions
 * (DEVIATIONS #5). FLTK group children use window-absolute coords.
 */
#include "colordialog.h"
#include "../core/palette.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Radio_Round_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>

namespace {

/* sample gradient strip: vertical, top row = value 255 (strong),
 * bottom = 0 (weak), drawn through the pending palette */
struct SampleStrip : Fl_Widget {
    uint8_t pal[256][3];

    SampleStrip(int x, int y, int w, int h) : Fl_Widget(x, y, w, h)
    {
        palette_build(0, 0, pal);
    }

    void draw() override
    {
        for (int row = 0; row < h(); row++) {
            int v = 255 * (h() - 1 - row) / (h() - 1);
            fl_color(pal[v][0], pal[v][1], pal[v][2]);
            fl_line(x(), y() + row, x() + w() - 1, y() + row);
        }
        fl_color(FL_DARK3);
        fl_rect(x(), y(), w(), h());
    }
};

struct Dlg {
    int ok;             /* 1 = OK pressed          */
    int mode, invert;   /* pending values          */
    SampleStrip *strip;
    Fl_Radio_Round_Button *r_mono, *r_temp, *r_blue;
    Fl_Check_Button *inv;
};

void refresh(Dlg *d)
{
    palette_build(d->mode, d->invert, d->strip->pal);
    d->strip->redraw();
}

void cb_radio(Fl_Widget *w, void *ud)
{
    Dlg *d = (Dlg *)ud;
    if (w == d->r_mono) d->mode = 0;
    if (w == d->r_temp) d->mode = 3;   /* 色温度 = mode 3 */
    if (w == d->r_blue) d->mode = 2;   /* ブルーレイ = mode 2 */
    refresh(d);
}

void cb_inv(Fl_Widget *w, void *ud)
{
    Dlg *d = (Dlg *)ud;
    d->invert = ((Fl_Check_Button *)w)->value() ? 1 : 0;
    refresh(d);
}

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

} /* namespace */

int color_dialog_run(int *mode, int *invert)
{
    Dlg d;
    d.ok = 0;
    d.mode = *mode;
    d.invert = *invert ? 1 : 0;

    /* Form5: client 280x111 */
    Fl_Double_Window *dlg = new Fl_Double_Window(280, 111,
                                                 "Color settings");
    dlg->set_modal();

    Fl_Box *lbl = new Fl_Box(22, 8, 45, 12, "Sample");
    lbl->labelsize(10);
    lbl = new Fl_Box(16, 24, 12, 12, "S");   /* strong (original: 強) */
    lbl->labelsize(10);
    lbl = new Fl_Box(16, 92, 12, 12, "W");   /* weak   (original: 弱) */
    lbl->labelsize(10);

    d.strip = new SampleStrip(32, 24, 25, 81);

    Fl_Group *grp = new Fl_Group(80, 8, 121, 73, "Type");
    grp->box(FL_ENGRAVED_BOX);
    grp->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    grp->labelsize(10);
    d.r_mono = new Fl_Radio_Round_Button(96, 24, 73, 17, "Monotone");
    d.r_temp = new Fl_Radio_Round_Button(96, 40, 73, 17, "Color temp");
    d.r_blue = new Fl_Radio_Round_Button(96, 56, 81, 17, "Blue ray");
    grp->end();
    d.r_mono->callback(cb_radio, &d);
    d.r_temp->callback(cb_radio, &d);
    d.r_blue->callback(cb_radio, &d);

    d.inv = new Fl_Check_Button(88, 88, 60, 17, "Invert");
    d.inv->callback(cb_inv, &d);

    Fl_Return_Button *ok = new Fl_Return_Button(208, 12, 65, 25, "OK");
    ok->callback(cb_ok, &d);
    Fl_Button *can = new Fl_Button(208, 44, 65, 25, "Cancel");
    can->callback(cb_cancel);

    dlg->end();

    /* preselect from the current values (non-2/3 mode -> Monotone,
     * like the original) */
    if (d.mode == 2) d.r_blue->setonly();
    else if (d.mode == 3) d.r_temp->setonly();
    else { d.r_mono->setonly(); d.mode = 0; }
    d.inv->value(d.invert);
    refresh(&d);

    dlg->show();
    while (dlg->shown())
        Fl::wait();

    int accepted = d.ok;
    if (accepted) {
        *mode = d.mode;
        *invert = d.invert;
    }
    delete dlg;
    return accepted;
}
