/* exportdialog.h - image save options (Form6 replica + a Format group).
 *
 * Kind radios: current display / 760x500 / 1140x750 / 2280x1500
 * (labels like the original; the numbers are max-height x width) plus
 * orientation Landscape/Portrait (Land. = 90-degree rotated export,
 * Port. = raster as-is - deliberately swapped from the original's
 * 横/縦 mapping, DEVIATIONS #14), per docs/01 sec. 4 "Save image".
 * The original's "output to clipboard" checkbox is omitted
 * (DEVIATIONS #12).
 *
 * The Format group (BMP / PNG) is our addition (DEVIATIONS #12,
 * amended 2026-08-09): the original's separate Form3 file-type choice
 * folded into the same dialog. Existing groups keep the Form6
 * coordinates; the window grew downward to make room.
 */
#ifndef ISOBAR_EXPORTDIALOG_H
#define ISOBAR_EXPORTDIALOG_H

/* Shows the dialog with kind (0..3), portrait (0/1) and fmt (0=BMP,
 * 1=PNG) preselected (pointers are in/out). Returns 1 on OK
 * (kind/portrait/fmt updated), 0 on Cancel. Kind 0 (current display)
 * has no orientation choice. */
int export_dialog_run(int *kind, int *portrait, int *fmt);

#endif
