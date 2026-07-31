/* colordialog.h - color processing dialog (Form5 replica, English).
 *
 * Radios Monotone / Color temp / Blue ray + an Invert checkbox, with a
 * live sample gradient strip (top = strong, bottom = weak), per
 * docs/01 sec. 4 "Color mapping". Edits a pending copy: OK commits,
 * Cancel fully reverts.
 */
#ifndef ISOBAR_COLORDIALOG_H
#define ISOBAR_COLORDIALOG_H

/* Shows the dialog with mode (0..3) and invert (0/1) preselected
 * (pointers are in/out). Mode 1 has no radio (dead UI path in the
 * original too); any non-2/3 mode shows as Monotone. Returns 1 on OK
 * (mode/invert updated), 0 on Cancel. */
int color_dialog_run(int *mode, int *invert);

#endif
