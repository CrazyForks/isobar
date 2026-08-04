/* noticedialog.cpp - see noticedialog.h.
 *
 * Form10 (docs/05-gui-layout.md): client 195x43, single centered label
 * 録音再設定中... -> English "Reconfiguring recording..." (DEVIATIONS
 * #5). The original positions it near the main window; we center it on
 * the parent. Plain FL_BORDER_BOX gives a visible frame on the
 * borderless window.
 */
#include "noticedialog.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>

namespace {
Fl_Double_Window *g_win = 0;
}

void show_notice(int parent_x, int parent_y, int parent_w, int parent_h)
{
    if (g_win)
        return;   /* already up */
    /* Form10: client 195x43 */
    g_win = new Fl_Double_Window(195, 43);
    g_win->border(0);   /* the original is a plain popup, no title bar  */
    Fl_Box *b = new Fl_Box(0, 0, 195, 43, "Reconfiguring recording...");
    b->box(FL_BORDER_BOX);
    b->labelsize(11);
    b->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    g_win->end();
    g_win->set_non_modal();
    /* center on the parent window (position() is absolute screen) */
    int px = parent_x + (parent_w - 195) / 2;
    int py = parent_y + (parent_h - 43) / 2;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    g_win->position(px, py);
    g_win->show();
    Fl::check();   /* pump so it paints before the blocking work */
}

void hide_notice()
{
    if (!g_win)
        return;
    g_win->hide();
    /* delete after a pump so the hide is processed */
    Fl::check();
    delete g_win;
    g_win = 0;
}
