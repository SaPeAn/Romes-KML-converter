/*
 *  app_core - log, settings, conversion worker thread and parsed-data access.
 *
 *  The converter functions (openkml, opencsv, savecsv, savekml, ...) are used unchanged.
 *  Their printf() and exit() calls are redirected here by the GUI_BUILD block of
 *  datatypes.h: printf lands in the log buffer below, exit unwinds to job_thread()
 *  through longjmp instead of terminating the process.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <setjmp.h>
#include <direct.h>
#include <process.h>
#include <windows.h>

#include "app_core.h"
#include "../open_file.h"
#include "../save_file.h"
#include "../kml_struct.h"
#include "html_report.h"

/*==================================== Log ====================================*/

static char             log_lines[APP_LOG_LINES][APP_LOG_WIDTH];
static int              log_count = 0;
static int              log_partial = 0;   /* last line has no newline yet, may be continued */
static CRITICAL_SECTION log_lock;
static int              log_ready = 0;

void app_log_init(void)
{
    if(!log_ready)
    {
        InitializeCriticalSection(&log_lock);
        log_ready = 1;
    }
    app_log_clear();
}

void app_log_clear(void)
{
    if(!log_ready) return;
    EnterCriticalSection(&log_lock);
    log_count = 0;
    log_partial = 0;
    log_lines[0][0] = 0;
    LeaveCriticalSection(&log_lock);
}

int app_log_count(void)
{
    int n;
    if(!log_ready) return 0;
    EnterCriticalSection(&log_lock);
    n = log_count + (log_partial ? 1 : 0);
    LeaveCriticalSection(&log_lock);
    return n;
}

const char* app_log_line(int index)
{
    if((index < 0) || (index >= APP_LOG_LINES)) return "";
    return log_lines[index];
}

/*----Appends text, splitting it into lines.  The converter prints one message in several
     calls (Saving csv-file.. then .. then ..ok), so an unfinished line stays open until
     its line break arrives.----*/
static void log_append(const char* text)
{
    if(!log_ready) return;
    EnterCriticalSection(&log_lock);
    while(*text != 0)
    {
        if(log_count >= (APP_LOG_LINES - 1)) break;   /* log is full, the rest is dropped */

        if(*text == 10)
        {
            if(log_partial) { log_count++; log_partial = 0; }
            else            { log_lines[log_count][0] = 0; log_count++; }
            log_lines[log_count][0] = 0;
        }
        else if(*text != 13)
        {
            size_t len = strlen(log_lines[log_count]);
            if(len < (APP_LOG_WIDTH - 1))
            {
                log_lines[log_count][len]     = *text;
                log_lines[log_count][len + 1] = 0;
            }
            log_partial = 1;
        }
        text++;
    }
    LeaveCriticalSection(&log_lock);
}

void app_log(const char* format, ...)
{
    char    buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_append(buffer);
}

void app_perror(const char* prefix)
{
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s: %s\n", prefix ? prefix : "", strerror(errno));
    log_append(buffer);
}

/*=============================== Fatal errors ================================*/

static jmp_buf      abort_jump;
static volatile int abort_armed = 0;

void app_abort(int code)
{
    (void)code;
    if(abort_armed) longjmp(abort_jump, 1);
    log_append("Internal error: abort outside of a conversion job\n");
}

/*----The converter files declare this as extern; a window has no stdin to flush.----*/
void clean_stdin(void)
{
}

/*================================= Settings ==================================*/

void app_settings_defaults(init_t* settings)
{
    settings->fillinflag      = 1;
    settings->GSMcoveragelvl  = -85.0;
    settings->UMTScoveragelvl = -90.0;
    settings->LTEcoveragelvl  = -83.5;
    settings->defaultcovlvl   = -85.0;
    settings->covercalctype   = total;
    settings->avgtype         = sma;
    settings->avgdepth        = 1;      /* 1 - без усреднения уровня */
    settings->maxskipdist     = 0.0;    /* 0 - без сглаживания покрытия */
}

int app_settings_load(const char* path, init_t* settings)
{
    char  str_1[100] = {0};
    char  str_2[100] = {0};
    FILE* file = fopen(path, "r");
    int   read_items;

    app_settings_defaults(settings);
    if(file == NULL) return 0;

    read_items = fscanf(file, "[OPTIONS]\nfilflag=%d\nGSMlevel=%lf\nUMTSlevel=%lf\nLTElevel=%lf\n"
                              "defaultlvl=%lf\ncalctype=%99s\navertype=%99s\naverdepth=%d\nmaxskipdistance=%lf\n",
                        &settings->fillinflag, &settings->GSMcoveragelvl, &settings->UMTScoveragelvl,
                        &settings->LTEcoveragelvl, &settings->defaultcovlvl, str_1, str_2,
                        &settings->avgdepth, &settings->maxskipdist);
    fclose(file);

    if(read_items != 9)
    {
        app_settings_defaults(settings);
        return 0;
    }

    if((strstr(str_1, "TOT") != NULL) || (strstr(str_1, "TOTAL") != NULL)) settings->covercalctype = total;
    else if((strstr(str_1, "SEP") != NULL) || (strstr(str_1, "SEPARATE") != NULL)) settings->covercalctype = separate;
    else settings->covercalctype = total;

    if(strstr(str_2, "SMA") != NULL) settings->avgtype = sma;
    else settings->avgtype = median;

    if(settings->avgdepth > 100) settings->avgdepth = 100;
    if(settings->avgdepth < 1)   settings->avgdepth = 1;   /* 1 - без усреднения */
    if((settings->fillinflag < 0) || (settings->fillinflag > 1)) settings->fillinflag = 1;
    if(settings->maxskipdist < 0.0) settings->maxskipdist = 0.0;

    return 1;
}

int app_settings_save(const char* path, const init_t* settings)
{
    FILE* file = fopen(path, "w");
    if(file == NULL) return 0;

    fprintf(file, "[OPTIONS]\nfilflag=%d\nGSMlevel=%.1f\nUMTSlevel=%.1f\nLTElevel=%.1f\ndefaultlvl=%.1f\n"
                  "calctype=%s\navertype=%s\naverdepth=%d\nmaxskipdistance=%.2f\n",
            settings->fillinflag, settings->GSMcoveragelvl, settings->UMTScoveragelvl,
            settings->LTEcoveragelvl, settings->defaultcovlvl,
            (settings->covercalctype == total) ? "TOT" : "SEP",
            (settings->avgtype == median) ? "MED" : "SMA",
            settings->avgdepth, settings->maxskipdist);
    fclose(file);
    return 1;
}

/*============================== Conversion job ===============================*/

/* The parsed data outlives the job: the table reads it cell by cell while the window is
   open, and it is released only when the next job starts. */
static folder_t     result_folders[MAX_FOLDERS];
static int          result_folder_quantity = 0;
static unsigned     result_row_count = 0;
static volatile int result_valid = 0;
static int          result_analysed = 0;              /* a coverage run has finished */
static int          result_selected[MAX_FOLDERS];    /* folders that took part in it */
static init_t       result_settings;                 /* settings it ran with */

/* Where the source file lives: the working directory is entered once, during the import,
   so the parsers can open the file by its bare name. */
static char work_base_name[APP_PATH_LEN];
static char work_directory[APP_PATH_LEN];

/* The csv the analysis works from - in the temporary folder while a kml has just been
   read, or the file the user opened - and the folder the results were written into. */
static char work_csv_path[APP_PATH_LEN];
static char result_dir[APP_PATH_LEN];
static char result_report_path[APP_PATH_LEN];

typedef enum {
    KIND_IMPORT,
    KIND_ANALYSE,
} JOB_KIND;

static struct {
    char           path[APP_PATH_LEN];
    SOURCE_TYPE    source;
    JOB_KIND       kind;
    init_t         settings;
    int            selected[MAX_FOLDERS];
    char           out_dir[APP_PATH_LEN];
    HANDLE         thread;
    HWND           notify;
    volatile int   state;
    volatile float progress;
    const char* volatile stage;
} job;

JOB_STATE   app_job_state(void)    { return (JOB_STATE)job.state; }
float       app_job_progress(void) { return job.progress; }
const char* app_job_stage(void)    { return (const char*)job.stage; }

static void job_step(float progress, const char* stage)
{
    job.progress = progress;
    job.stage    = stage;
}

/*----Splits the chosen path the way the converter expects it: it works in the current
     directory and appends the extension to a bare file name by itself.----*/
static void split_path(const char* full_path, char* directory, char* base_name)
{
    const char* back_slash = strrchr(full_path, 92);
    const char* fwd_slash  = strrchr(full_path, 47);
    const char* slash = (fwd_slash > back_slash) ? fwd_slash : back_slash;
    const char* dot;
    size_t      dir_len;
    size_t      name_len;

    if(slash != NULL)
    {
        dir_len = (size_t)(slash - full_path);
        if(dir_len >= APP_PATH_LEN) dir_len = APP_PATH_LEN - 1;
        memcpy(directory, full_path, dir_len);
        directory[dir_len] = 0;
        full_path = slash + 1;
    }
    else
    {
        directory[0] = 0;
    }

    name_len = strlen(full_path);
    if(name_len > APP_PATH_LEN - 1) name_len = APP_PATH_LEN - 1;
    memcpy(base_name, full_path, name_len);
    base_name[name_len] = 0;
    dot = strrchr(base_name, 46);
    if(dot != NULL) base_name[dot - base_name] = 0;
}

static void release_result(void)
{
    if(result_folder_quantity > 0)
    {
        /* MAX_FOLDERS, not folder_quantity: opencsv() also allocates the reserved slot
           folder[folder_quantity] for the averaged coverage data. */
        foldermemfree(result_folders, MAX_FOLDERS);
    }
    memset(result_folders, 0, sizeof(result_folders));
    result_folder_quantity = 0;
    result_row_count = 0;
    result_valid = 0;
    result_analysed = 0;
}

static void report_totals(const init_t* settings)
{
    int gps = gpsfolderindex(result_folders, result_folder_quantity);
    int avg = result_folder_quantity;      /* the reserved slot holds the averaged coverage */
    if(gps < 0) gps = 0;

    if(settings->covercalctype == total)
    {
        app_log("\nИтог (общий расчёт, сглаживание по расстоянию):\n");
        app_log("  всего        %.3f км\n", result_folders[avg].totdist);
        app_log("  с покрытием  %.3f км\n", result_folders[avg].covtotdist);
        app_log("  без покрытия %.3f км\n", result_folders[avg].uncovtotdist);
        app_log("  без покрытия без сглаживания по расстоянию: %.3f км\n", result_folders[gps].uncovtotdist);
    }
    else
    {
        app_log("\nИтог по элементам:\n");
        for(int i = 0; i < result_folder_quantity; i++)
        {
            if(i == gps) continue;
            if(result_folders[i].reg_quantity == 0) continue;   /* this folder was not selected */
            app_log("  %s: всего %.3f км, без покрытия %.3f км, участков %u\n",
                    result_folders[i].short_name, result_folders[i].totdist,
                    result_folders[i].uncovtotdist, result_folders[i].reg_quantity);
        }
    }
}
/*----Builds "<folder>\<name><suffix>" without ever writing past the end of the buffer.
     snprintf() would do the same, but the compiler cannot see that two paths never fill
     it at once and buries the build in truncation warnings.----*/
static void join_path(char* target, int size, const char* folder, const char* name,
                      const char* suffix)
{
    int used;

    target[0] = 0;
    if((folder != NULL) && (folder[0] != 0))
    {
        strncat(target, folder, (size_t)(size - 1));
        used = (int)strlen(target);
        if((used > 0) && (target[used - 1] != '\\') && (target[used - 1] != '/')
           && (used + 1 < size))
        {
            target[used] = '\\';
            target[used + 1] = 0;
        }
    }
    used = (int)strlen(target);
    if(name != NULL) strncat(target, name, (size_t)(size - used - 1));
    used = (int)strlen(target);
    if(suffix != NULL) strncat(target, suffix, (size_t)(size - used - 1));
}

/*----The folder for the csv that only the program itself needs.  Opening a file used to
     leave Output_CSV_files and Output_KML_files behind; now nothing is written where the
     user can see it until the coverage is calculated and a folder is picked.----*/
static void temp_folder(char* buffer, int size)
{
    char  base[APP_PATH_LEN];
    DWORD length = GetTempPathA(sizeof(base), base);

    if((length == 0) || (length >= sizeof(base))) strcpy(base, ".\\");
    snprintf(buffer, size, "%sRomesCov", base);
    _mkdir(buffer);
}


/*----Step one: read the source and make the measurements available.  A kml is converted to
     csv on the way, because everything downstream works from a csv.  Nothing else is
     written here - the reports wait until the folders are picked.----*/
static void run_import(void)
{
    char directory[APP_PATH_LEN];

    split_path(job.path, directory, work_base_name);

    if(directory[0] != 0)
    {
        if(!SetCurrentDirectoryA(directory))
        {
            app_log("Ошибка! Не удалось перейти в папку %s\n", directory);
            app_abort(1);
        }
    }
    strncpy(work_directory, directory, APP_PATH_LEN - 1);
    work_directory[APP_PATH_LEN - 1] = 0;

    app_log("Папка: %s\n", directory[0] ? directory : ".");
    app_log("Файл: %s\n", work_base_name);

    if(job.source == SOURCE_KML)
    {
        /* The csv everything downstream reads is made here, but it waits in the temporary
           folder until the user says where the results should go. */
        char temp_dir[APP_PATH_LEN];

        temp_folder(temp_dir, sizeof(temp_dir));
        set_output_dirs(temp_dir, temp_dir);
        set_csv_input_dir(temp_dir);
        join_path(work_csv_path, sizeof(work_csv_path), temp_dir, work_base_name, ".csv");

        job_step(0.05f, "Чтение kml-файла");
        result_folder_quantity = openkml(work_base_name, result_folders);

        job_step(0.55f, "Подготовка данных");
        savecsv(work_base_name, result_folders, result_folder_quantity, job.settings.fillinflag);
        foldermemfree(result_folders, result_folder_quantity);
        memset(result_folders, 0, sizeof(result_folders));

        job_step(0.80f, "Чтение данных");
        result_folder_quantity = opencsv(work_base_name, result_folders);
    }
    else
    {
        /* A csv is read where it lies and copied into the results folder later. */
        set_csv_input_dir(".");
        join_path(work_csv_path, sizeof(work_csv_path),
                  directory[0] ? directory : ".", work_base_name, ".csv");

        job_step(0.10f, "Чтение csv-файла");
        result_folder_quantity = opencsv(work_base_name, result_folders);
    }

    result_row_count = result_folders[0].placemark_quantity;
    result_valid = 1;
    job_step(1.0f, "Данные загружены");
    app_log("\nЗагружено точек: %u, элементов: %d\n", result_row_count, result_folder_quantity);
    app_log("Отметьте элементы галочками в списке и нажмите расчёт покрытия.\n");
}

/*----Step two: everything the console version did after reading the csv, but only for the
     folders the user ticked.  They are gathered into a separate array that shares the
     measurement arrays with result_folders, so the converter functions see a file made of
     just those folders plus the track.----*/
static void run_analyse(void)
{
    folder_t sel[MAX_FOLDERS];
    int      map[MAX_FOLDERS];
    int      selected_count = 0;
    int      gps = gpsfolderindex(result_folders, result_folder_quantity);
    int      i;

    if(!result_valid)
    {
        app_log("Сначала откройте файл с данными\n");
        app_abort(1);
    }

    /* Regions of a previous analysis are released here; this run allocates its own. */
    for(i = 0; i < MAX_FOLDERS; i++)
    {
        free(result_folders[i].covreg);
        result_folders[i].covreg = NULL;
        result_folders[i].reg_quantity = 0;
    }

    memset(sel, 0, sizeof(sel));
    for(i = 0; i < result_folder_quantity; i++)
    {
        if((i != gps) && !job.selected[i]) continue;
        sel[selected_count] = result_folders[i];   /* shallow copy: the points are shared on purpose */
        map[selected_count] = i;
        selected_count++;
    }

    if((selected_count == 0) || ((gps >= 0) && (selected_count == 1)))
    {
        app_log("Не отмечено ни одного элемента с уровнями сигнала\n");
        app_abort(1);
    }

    /* The slot next to the data folders carries the averaged coverage. */
    sel[selected_count] = result_folders[result_folder_quantity];

    /* Everything this run produces goes into the folder the user has just picked: the csv
       the measurements came from, the coverage table, the html report, and the kml files
       of the elements in a subfolder of their own. */
    {
        char kml_dir[APP_PATH_LEN];
        char csv_target[APP_PATH_LEN];

        strncpy(result_dir, job.out_dir, APP_PATH_LEN - 1);
        result_dir[APP_PATH_LEN - 1] = 0;
        _mkdir(result_dir);

        join_path(kml_dir, sizeof(kml_dir), result_dir, work_base_name, "_kml");
        _mkdir(kml_dir);
        set_output_dirs(result_dir, kml_dir);

        join_path(csv_target, sizeof(csv_target), result_dir, work_base_name, ".csv");
        if(_stricmp(csv_target, work_csv_path) != 0)
        {
            if(!CopyFileA(work_csv_path, csv_target, FALSE))
                app_log("Не удалось сохранить %s\n", csv_target);
        }
        app_log("Папка результатов: %s\n", result_dir);
    }

    app_log("Расчёт по элементам:\n");
    for(i = 0; i < selected_count; i++)
    {
        if(map[i] == gps) continue;
        app_log("  %s\n", sel[i].name);
    }

    job_step(0.15f, "Усреднение уровней");
    getaverlvl(sel, selected_count, job.settings.avgdepth, job.settings.avgtype);

    job_step(0.35f, "Расчёт покрытия");
    createcovtab(sel, selected_count, job.settings);

    job_step(0.55f, "Сохранение kml-файлов");
    savekml(work_base_name, sel, selected_count);

    job_step(0.85f, "Сохранение таблицы покрытия");
    savecsvcovtab(work_base_name, sel, selected_count, job.settings);

    /* The results travel back, so that release_result() frees the regions later. */
    for(i = 0; i < selected_count; i++)
    {
        result_folders[map[i]].covreg       = sel[i].covreg;
        result_folders[map[i]].reg_quantity = sel[i].reg_quantity;
        result_folders[map[i]].totdist      = sel[i].totdist;
        result_folders[map[i]].covtotdist   = sel[i].covtotdist;
        result_folders[map[i]].uncovtotdist = sel[i].uncovtotdist;
        result_folders[map[i]].threshold    = sel[i].threshold;
    }
    result_folders[result_folder_quantity] = sel[selected_count];

    memcpy(result_selected, job.selected, sizeof(result_selected));
    result_settings  = job.settings;
    result_analysed  = 1;

    /* The html report is part of the run now, not a separate button: it goes into the
       same folder as the tables, and the button only opens it. */
    job_step(0.95f, "Сохранение html-отчёта");
    join_path(result_report_path, sizeof(result_report_path), result_dir, work_base_name,
              "_report.html");
    if(!html_report_write(result_report_path))
    {
        app_log("Не удалось записать html-отчёт\n");
        result_report_path[0] = 0;
    }

    report_totals(&job.settings);
    job_step(1.0f, "Готово");
}

static unsigned __stdcall job_thread(void* data)
{
    (void)data;

    abort_armed = 1;
    if(setjmp(abort_jump) == 0)
    {
        if(job.kind == KIND_IMPORT) run_import();
        else                        run_analyse();
        job.state = JOB_DONE;
    }
    else
    {
        job.state = JOB_FAILED;
        job.stage = "Ошибка";
        if(job.kind == KIND_IMPORT) result_valid = 0;
    }
    abort_armed = 0;

    if(job.notify != NULL) PostMessage(job.notify, WM_APP_JOB_FINISHED, 0, 0);
    return 0;
}

static int job_launch(HWND notify_window)
{
    job.notify   = notify_window;
    job.progress = 0.0f;
    job.stage    = "Запуск";
    job.state    = JOB_RUNNING;

    job.thread = (HANDLE)_beginthreadex(NULL, 0, job_thread, NULL, 0, NULL);
    if(job.thread == NULL)
    {
        job.state = JOB_FAILED;
        app_log("Не удалось создать рабочий поток\n");
        return 0;
    }
    return 1;
}

int app_job_import(const char* file_path, SOURCE_TYPE source, HWND notify_window)
{
    if(job.state == JOB_RUNNING) return 0;

    app_job_join();
    release_result();
    app_log_clear();

    strncpy(job.path, file_path, APP_PATH_LEN - 1);
    job.path[APP_PATH_LEN - 1] = 0;
    job.source = source;
    job.kind   = KIND_IMPORT;
    app_settings_defaults(&job.settings);   /* of these, only fillinflag matters for the import */

    return job_launch(notify_window);
}

int app_job_analyse(const init_t* settings, const int* selected, const char* out_dir,
                    HWND notify_window)
{
    int i;

    if(job.state == JOB_RUNNING) return 0;
    if(!result_valid) return 0;

    app_job_join();
    app_log_clear();

    job.settings = *settings;
    job.kind     = KIND_ANALYSE;
    for(i = 0; i < MAX_FOLDERS; i++) job.selected[i] = (selected != NULL) ? selected[i] : 1;

    strncpy(job.out_dir, ((out_dir != NULL) && (out_dir[0] != 0)) ? out_dir : ".",
            APP_PATH_LEN - 1);
    job.out_dir[APP_PATH_LEN - 1] = 0;

    return job_launch(notify_window);
}

void app_job_join(void)
{
    if(job.thread != NULL)
    {
        WaitForSingleObject(job.thread, INFINITE);
        CloseHandle(job.thread);
        job.thread = NULL;
    }
}

/*=============================== Parsed data =================================*/

int      app_result_ready(void)   { return result_valid; }
int      app_result_folders(void) { return result_valid ? result_folder_quantity : 0; }
unsigned app_result_rows(void)    { return result_valid ? result_row_count : 0; }

const char* app_result_folder_name(int folder)
{
    if(!result_valid || (folder < 0) || (folder >= result_folder_quantity)) return "";
    return result_folders[folder].short_name;
}

const char* app_result_folder_fullname(int folder)
{
    if(!result_valid || (folder < 0) || (folder >= result_folder_quantity)) return "";
    return result_folders[folder].name;
}

int app_result_gps_folder(void)
{
    if(!result_valid) return -1;
    return gpsfolderindex(result_folders, result_folder_quantity);
}

const char* app_result_field_name(int field)
{
    static const char* names[APP_FIELDS] = {
        "name", "measnumber", "timestamp", "signallevel", "latitude", "longitude", "altitude", "color"
    };
    if((field < 0) || (field >= APP_FIELDS)) return "";
    return names[field];
}

/*----One cell of the table, formatted exactly as savecsv() writes it.----*/
void app_result_cell(unsigned row, int folder, int field, char* buffer, int size)
{
    const placemark_t* placemark;

    buffer[0] = 0;
    if(!result_valid || (folder < 0) || (folder >= result_folder_quantity)) return;
    if(row >= result_row_count) return;

    placemark = &result_folders[folder].placemark_arr[row];
    switch(field)
    {
    case 0: snprintf(buffer, size, "%s", placemark->name);        break;
    case 1: snprintf(buffer, size, "%d", placemark->measnumber);  break;
    case 2: snprintf(buffer, size, "%d", placemark->timestamp);   break;
    case 3: if(placemark->hasdata) snprintf(buffer, size, "%f", placemark->signallevel);  /* empty cell = no measurement */
            break;
    case 4: snprintf(buffer, size, "%f", placemark->latitude);    break;
    case 5: snprintf(buffer, size, "%f", placemark->longitude);   break;
    case 6: snprintf(buffer, size, "%f", placemark->altitude);    break;
    case 7: snprintf(buffer, size, "%s", placemark->color);       break;
    default: break;
    }
}

const folder_t* app_result_folder(int index)
{
    if(!result_valid || (index < 0) || (index > result_folder_quantity)) return NULL;
    return &result_folders[index];
}

int app_result_analysed(void)
{
    return (result_valid && result_analysed) ? 1 : 0;
}

int app_result_selected(int folder)
{
    int gps = gpsfolderindex(result_folders, result_folder_quantity);
    if(!result_analysed || (folder < 0) || (folder >= result_folder_quantity)) return 0;
    if(folder == gps) return 0;              /* the track carries no levels of its own */
    return result_selected[folder] ? 1 : 0;
}

const char* app_result_source_name(void)
{
    return work_base_name;
}

const init_t* app_result_settings(void)
{
    return &result_settings;
}

/*----The analysis writes the report into the folder the user picked; the window only
     needs to know where it ended up.----*/
const char* app_result_report_path(void)
{
    if(!result_analysed) return NULL;
    return (result_report_path[0] != 0) ? result_report_path : NULL;
}
