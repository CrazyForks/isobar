/* exportdialog.h - image save options (Form6 replica, English).
 *
 * Kind radios: current display / 760x500 / 1140x750 / 2280x1500
 * (labels like the original; the numbers are max-height x width) plus
 * orientation Landscape/Portrait (Land. = 90-degree rotated export,
 * Port. = raster as-is - deliberately swapped from the original's
 * 横/縦 mapping, DEVIATIONS #14), per docs/01 sec. 4 "Save image".
 * The original's "output to clipboard" checkbox is omitted
 * (DEVIATIONS #12).
 */
#ifndef ISOBAR_EXPORTDIALOG_H
#define ISOBAR_EXPORTDIALOG_H

/* Shows the dialog with kind (0..3) and portrait (0/1) preselected
 * (pointers are in/out). Returns 1 on OK (kind/portrait updated),
 * 0 on Cancel. Kind 0 (current display) has no orientation choice. */
int export_dialog_run(int *kind, int *portrait);

#endif
