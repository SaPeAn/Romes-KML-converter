/*
 *  app_core - the non-graphical half of the gui build.
 *
 *  It holds what the window needs but the converter does not provide: the log buffer
 *  that replaces the console, the settings.ini reader/writer, the conversion job that
 *  runs in a worker thread, and access to the parsed data so the window can show it
 *  as a table.  Nothing here depends on the widget toolkit - only on Win32 threads.
 */

#ifndef __APP_CORE_H__
#define __APP_CORE_H__

#include <windows.h>
#include "../datatypes.h"

#define APP_LOG_LINES   2048
#define APP_LOG_WIDTH   256
#define APP_PATH_LEN    1024
#define APP_FIELDS      8       /* name;measnumber;timestamp;signallevel;latitude;longitude;altitude;color */

/* Posted to the window given to app_job_start() when the worker thread finishes. */
#define WM_APP_JOB_FINISHED  (WM_APP + 1)

typedef enum {
    JOB_IDLE,
    JOB_RUNNING,
    JOB_DONE,
    JOB_FAILED,
} JOB_STATE;

typedef enum {
    SOURCE_KML,
    SOURCE_CSV,
} SOURCE_TYPE;

/*----Log: filled from the worker thread, read from the gui thread----*/
void   app_log_init(void);
void   app_log_clear(void);
int    app_log_count(void);
const char* app_log_line(int index);

/*----State of the conversion job----*/
JOB_STATE   app_job_state(void);
float       app_job_progress(void);            // 0.0 ... 1.0
const char* app_job_stage(void);

/*----Settings, compatible with the settings.ini of the console build----*/
void   app_settings_defaults(init_t* settings);
int    app_settings_load(const char* path, init_t* settings);   // 1 - ok, 0 - missing or malformed
int    app_settings_save(const char* path, const init_t* settings);

/*----The work is split in two steps, so that the folders can be picked in between.

     app_job_import()  - reads the source (a kml is converted to csv on the way) and makes
                         the measurements available for the table.  Nothing else is written.
     app_job_analyse() - the rest of the original algorithm (averaging, coverage, kml and
                         csv reports), carried out only for the folders marked in
                         "selected", an array of app_result_folders() flags.  The GPS
                         folder is always taken along: it carries the track.

     file_path is in the system ANSI encoding, as fopen() expects it.  When a job ends,
     WM_APP_JOB_FINISHED is posted to notify_window.----*/
int    app_job_import(const char* file_path, SOURCE_TYPE source, HWND notify_window);
int    app_job_analyse(const init_t* settings, const int* selected, const char* out_dir,
                      HWND notify_window);
void   app_job_join(void);

/*----Parsed data, kept alive after the job so the table can read it on demand.
     The rows repeat the layout of the csv file: every folder contributes APP_FIELDS
     columns.  Freed when the next job starts.----*/
int         app_result_ready(void);
int         app_result_folders(void);
unsigned    app_result_rows(void);
const char* app_result_folder_name(int folder);        /* short name, safe for file names */
const char* app_result_folder_fullname(int folder);    /* name as it stands in the source file */
int         app_result_gps_folder(void);               /* index of the track folder, or -1 */
const char* app_result_field_name(int field);
void        app_result_cell(unsigned row, int folder, int field, char* buffer, int size);

/*----Everything the html report needs.  app_result_folder() hands out the parsed folder
     itself: index 0..app_result_folders()-1 for the data folders, app_result_folders()
     for the slot that holds the averaged coverage.----*/
const folder_t* app_result_folder(int index);
int             app_result_analysed(void);          /* 1 after a coverage run has succeeded */
int             app_result_selected(int folder);    /* was this folder part of that run */
const char*     app_result_source_name(void);       /* file name without the extension */
const init_t*   app_result_settings(void);          /* settings the analysis ran with */

/*----Path of the html report the last analysis wrote, or NULL when there is none: the
     report is made by the run itself, in the folder picked for the results.----*/
const char*     app_result_report_path(void);

#endif
