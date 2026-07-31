/* noticedialog.h - Form10 replica (recording-device-change notice).
 *
 * Form10 (docs/05-gui-layout.md): a tiny 195x43 window, caption
 * 録音デバイス変更 ("Recording device change"), single label
 * 録音再設定中... ("Reconfiguring recording..."). The original shows it
 * while the audio input device is being switched mid-reception.
 */
#ifndef ISOBAR_NOTICEDIALOG_H
#define ISOBAR_NOTICEDIALOG_H

/* Show the notice window (non-modal, centered on the parent) and keep
 * it up until hide_notice() is called. Idempotent (a second show while
 * already up is a no-op). parent_w/h center the notice on that window. */
void show_notice(int parent_w, int parent_h);

/* Hide and tear down the notice window if it is up. Safe to call when
 * not shown. */
void hide_notice();

#endif
