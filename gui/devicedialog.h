/* devicedialog.h - input device chooser (Form9 replica, English).
 *
 * Modal dialog listing the system's audio input devices; row 0 is
 * "Default input device" (the original's first row). Returns the
 * chosen row index for the [Wave] WaveDev ini setting.
 */
#ifndef ISOBAR_DEVICEDIALOG_H
#define ISOBAR_DEVICEDIALOG_H

/* Shows the dialog with `current` preselected (0 = default device).
 * Returns the new selection, or -1 if cancelled. */
int device_dialog_run(int current);

#endif
