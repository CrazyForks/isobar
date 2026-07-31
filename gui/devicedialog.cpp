/* devicedialog.cpp - see devicedialog.h.
 *
 * Layout follows Form9 (docs/05-gui-layout.md): list at (8,8,273,121),
 * buttons at x=288. English captions (DEVIATIONS #5). The original's
 * "Volume control" button opens the Windows mixer; macOS has no such
 * per-device system panel, so the button is kept (layout fidelity)
 * but deactivated.
 */
#include "devicedialog.h"
#include "audio.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Button.H>
#include <FL/fl_ask.H>

namespace {
struct Dlg {
    int sel;   /* chosen row, -1 = cancelled */
};
}

static void cb_select(Fl_Widget *w, void *ud)
{
    Dlg *d = (Dlg *)ud;
    Fl_Browser *b = (Fl_Browser *)w->parent()->child(0);
    d->sel = b->value() - 1;   /* browser rows are 1-based */
    w->window()->hide();
}

static void cb_cancel_dlg(Fl_Widget *w, void *)
{
    w->window()->hide();
}

int device_dialog_run(int current)
{
    std::vector<AudioDeviceInfo> devs = audio_input_devices();
    if (devs.empty()) {
        fl_alert("no audio input devices found");
        return -1;
    }

    Dlg d;
    d.sel = -1;

    /* Form9: client 384x135 */
    Fl_Double_Window *dlg = new Fl_Double_Window(384, 135, "Input device");
    dlg->set_modal();

    Fl_Hold_Browser *list = new Fl_Hold_Browser(8, 8, 273, 121);
    list->textsize(11);
    list->add("Default input device");
    for (const AudioDeviceInfo &dev : devs) {
        std::string label = dev.name;
        if (dev.is_default)
            label += "  (default)";
        list->add(label.c_str());
    }
    if (current < 0 || current > (int)devs.size())
        current = 0;
    list->value(current + 1);

    Fl_Return_Button *sel = new Fl_Return_Button(288, 8, 89, 25, "Select");
    sel->callback(cb_select, &d);
    Fl_Button *vol = new Fl_Button(288, 72, 89, 25, "Volume…");
    vol->deactivate();   /* Windows-mixer feature; no macOS equivalent */
    Fl_Button *can = new Fl_Button(288, 104, 89, 25, "Cancel");
    can->callback(cb_cancel_dlg);

    dlg->end();
    dlg->show();
    while (dlg->shown())
        Fl::wait();

    int result = d.sel;
    delete dlg;
    return result;
}
