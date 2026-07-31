/* progressdialog.cpp - see progressdialog.h.
 *
 * Form2 (docs/05-gui-layout.md): client 328x48, a label 処理中... ->
 * English "Processing..." (DEVIATIONS #5) above an Fl_Progress bar.
 * The original's Panel1 fills the window; we replicate the layout.
 */
#include "progressdialog.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>

struct ProgressWin {
    Fl_Double_Window *win;
    Fl_Progress *bar;
};

ProgressScope::ProgressScope(int parent_w, int parent_h) : win(0)
{
    ProgressWin *pw = new ProgressWin;
    /* Form2: client 328x48 */
    pw->win = new Fl_Double_Window(328, 48, "Processing...");
    pw->win->set_non_modal();
    /* Label1 (8,8,313,17) */
    Fl_Box *lbl = new Fl_Box(8, 8, 313, 17, "Processing...");
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lbl->labelsize(11);
    /* ProgressBar1 (8,24,313,17) */
    pw->bar = new Fl_Progress(8, 24, 313, 17);
    pw->bar->minimum(0);
    pw->bar->maximum(1);
    pw->bar->value(0);
    pw->win->end();
    int px = (parent_w - 328) / 2;
    int py = (parent_h - 48) / 2;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    pw->win->position(px, py);
    pw->win->show();
    Fl::check();   /* paint before the work begins */
    win = pw;
}

ProgressScope::~ProgressScope()
{
    if (!win)
        return;
    ProgressWin *pw = (ProgressWin *)win;
    pw->win->hide();
    Fl::check();   /* process the hide */
    delete pw->win;
    delete pw;
    win = 0;
}

void ProgressScope::update(double fraction)
{
    if (!win)
        return;
    ProgressWin *pw = (ProgressWin *)win;
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    pw->bar->value(fraction);
    Fl::check();   /* keep the window responsive + repainted */
}
