/* autosavedialog.h - auto-save settings (Form8 replica, English).
 *
 * Form8 (docs/05-gui-layout.md): auto-save folder, the output FORMAT
 * (original's FilterComboBox1: *.syn / *.bmp / *.jpg; we offer
 * syn/bmp/png - JPEG dropped, DEVIATIONS #15; PNG added 2026-08-09,
 * always grayscale), CycleGet checkbox ("restart scan at max width"),
 * saved-image-size radios, OK/Cancel. The original's drive/dir/file
 * listboxes are replaced by a read-only path field + Browse... (native
 * chooser, DEVIATIONS #13).
 */
#ifndef ISOBAR_AUTOSAVEDIALOG_H
#define ISOBAR_AUTOSAVEDIALOG_H

#include <string>

/* Shows the dialog modally with the current values (pointers are
 * in/out). Returns 1 on OK (values updated), 0 on Cancel.
 * fmt  = output format: 0 = .syn (default), 1 = .bmp, 2 = .png (gray)
 *        - runtime only, no ini key (the original's filter index is
 *        runtime-only too).
 * size = image-size radio index 0..2 (760x500 / 1140x750 / 2280x1500)
 *        - meaningful only when fmt is bmp or png; runtime only, no
 *        ini key (docs/01 sec. 4 Form8). */
int autosave_dialog_run(std::string *dirname, int *cycleget,
                        int *fmt, int *size);

#endif
