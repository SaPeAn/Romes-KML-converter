/*
 *  html_report - a detailed report with per-folder charts.
 *
 *  The charts show what the _coverchart.csv holds: for every folder the measurement
 *  number along x and the level along y - the threshold, the measured level and the
 *  averaged level.  The page is a single self-contained html file with inline svg,
 *  so it opens in any browser without anything else next to it.
 */

#ifndef __HTML_REPORT_H__
#define __HTML_REPORT_H__

/* Writes the report of the last analysis.  Returns 1 on success. */
int html_report_write(const char* path);

#endif
