/* progressdialog.h - Form2 replica (processing/progress dialog).
 *
 * Form2 (docs/05-gui-layout.md): client 328x48, caption 処理中...
 * ("Processing..."), a label + a TProgressBar. The original shows it
 * during every buffer-clear / save / print render (docs/01 sec. 4).
 * On modern hardware these are near-instant, so the dialog only flashes
 * briefly; we keep it lightweight.
 *
 * Usage is RAII: construct a ProgressScope around the work, call
 * update(fraction) as it proceeds, destroy to dismiss.
 */
#ifndef ISOBAR_PROGRESSDIALOG_H
#define ISOBAR_PROGRESSDIALOG_H

/* Shows the dialog (centered on the parent at parent_x/y/w/h) with
 * "Processing...". Destruct to hide + tear down. While up, the scope
 * pumps the event loop in update() so the window repaints. */
class ProgressScope {
public:
    ProgressScope(int parent_x, int parent_y, int parent_w, int parent_h);
    ~ProgressScope();
    ProgressScope(const ProgressScope &) = delete;
    ProgressScope &operator=(const ProgressScope &) = delete;
    /* Set the bar to fraction (0.0..1.0) and pump the event loop so the
     * window repaints. Cheap to call often. */
    void update(double fraction);
private:
    void *win;   /* Fl_Double_Window* (opaque to keep the header light) */
};

#endif
