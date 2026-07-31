/* exportdialog.cpp - see exportdialog.h.
 *
 * Layout follows Form6 (docs/05-gui-layout.md): 290x103 client, kind
 * group at (8,8,129,89), orientation group at (144,8,137,41), OK at
 * (144,72), Cancel at (216,72). Choosing kind 0 disables orientation
 * (like the original). English captions (DEVIATIONS #5).
 */
#include "exportdialog.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Radio_Round_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Button.H>

namespace {

struct Dlg {
    int ok;
    int kind, portrait;
    Fl_Radio_Round_Button *k0, *k1, *k2, *k3;
    Fl_Radio_Round_Button *o_land, *o_port;
};

void cb_kind(Fl_Widget *w, void *ud)
{
    Dlg *d = (Dlg *)ud;
    if (w == d->k0) d->kind = 0;
    if (w == d->k1) d->kind = 1;
    if (w == d->k2) d->kind = 2;
    if (w == d->k3) d->kind = 3;
    /* kind 0 = current display: orientation meaningless, disabled */
    if (d->kind == 0) {
        d->o_land->deactivate();
        d->o_port->deactivate();
    } else {
        d->o_land->activate();
        d->o_port->activate();
    }
}

void cb_orient(Fl_Widget *w, void *ud)
{
    Dlg *d = (Dlg *)ud;
    d->portrait = (w == d->o_port) ? 1 : 0;
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

int export_dialog_run(int *kind, int *portrait)
{
    Dlg d;
    d.ok = 0;
    d.kind = *kind;
    d.portrait = *portrait;

    /* Form6: client 290x103 */
    Fl_Double_Window *dlg = new Fl_Double_Window(290, 103, "Bitmap type");
    dlg->set_modal();

    Fl_Group *kg = new Fl_Group(8, 8, 129, 89, "Image type");
    kg->box(FL_ENGRAVED_BOX);
    kg->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    kg->labelsize(10);
    d.k0 = new Fl_Radio_Round_Button(16, 24, 113, 17, "Current view");
    d.k1 = new Fl_Radio_Round_Button(16, 40, 113, 17, "760x500 Pixel");
    d.k2 = new Fl_Radio_Round_Button(16, 56, 113, 17, "1140x750 Pixel");
    d.k3 = new Fl_Radio_Round_Button(16, 72, 113, 17, "2280x1500 Pixel");
    kg->end();

    Fl_Group *og = new Fl_Group(144, 8, 137, 41, "Orientation");
    og->box(FL_ENGRAVED_BOX);
    og->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    og->labelsize(10);
    d.o_land = new Fl_Radio_Round_Button(160, 24, 41, 17, "Land.");
    d.o_port = new Fl_Radio_Round_Button(224, 24, 41, 17, "Port.");
    og->end();

    d.k0->callback(cb_kind, &d);
    d.k1->callback(cb_kind, &d);
    d.k2->callback(cb_kind, &d);
    d.k3->callback(cb_kind, &d);
    d.o_land->callback(cb_orient, &d);
    d.o_port->callback(cb_orient, &d);

    Fl_Return_Button *ok = new Fl_Return_Button(144, 72, 65, 25, "OK");
    ok->callback(cb_ok, &d);
    Fl_Button *can = new Fl_Button(216, 72, 65, 25, "Cancel");
    can->callback(cb_cancel);

    dlg->end();

    if (d.kind < 0 || d.kind > 3)
        d.kind = 0;
    Fl_Radio_Round_Button *kr[4] = { d.k0, d.k1, d.k2, d.k3 };
    kr[d.kind]->setonly();
    if (d.portrait) d.o_port->setonly();
    else            d.o_land->setonly();
    cb_kind(kr[d.kind], &d);   /* apply the kind-0 disable rule */

    dlg->show();
    while (dlg->shown())
        Fl::wait();

    int accepted = d.ok;
    if (accepted) {
        *kind = d.kind;
        *portrait = d.portrait;
    }
    delete dlg;
    return accepted;
}
