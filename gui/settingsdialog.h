/* settingsdialog.h - "Details" settings dialog (original Form4).
 *
 * Layout follows docs/05-gui-layout.md Form4 (320x156 client area),
 * English captions per the mapping table there. The port adds OK and
 * Cancel buttons below the groups, so the window is 320x192 (noted in
 * docs/05).
 */
#ifndef ISOBAR_SETTINGSDIALOG_H
#define ISOBAR_SETTINGSDIALOG_H

#include "../core/settings.h"

/* Modal dialog. Edits a copy of s; on OK, commits to s, saves to
 * settings_path() and returns true. On Cancel (or window close)
 * returns false and leaves s untouched. */
bool settings_dialog_run(KgSettings &s);

#endif
