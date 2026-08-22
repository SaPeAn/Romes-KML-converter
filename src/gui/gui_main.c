/*
 *  gui_main - WinAPI window for RomesCov (no external toolkits).
 *
 *  Replaces the console menu of main.c: the file is picked in the system dialog, every
 *  settings.ini option is edited in place, the output the converter used to print goes
 *  to the log pane, and the parsed measurements are shown as a table.
 *
 *  The table keeps one row per measurement: the four values shared by all folders come
 *  first (row number, timestamp and the position taken from the GPS track), followed by
 *  one signal level column per folder, headed with the full folder name.  The GPS folder
 *  has no column of its own - it carries no level, and its coordinates are already there.
 *
 *  It is a virtual list view (LVS_OWNERDATA): 17 000 rows are never copied into the
 *  control, it asks app_core for a cell only when it draws one.
 *
 *  This file is windows-1251, like main.c, and uses the ANSI flavour of the API, so the
 *  Russian captions reach the controls unchanged.
 */

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "app_core.h"
#include "map_view.h"
#include "about.h"
#include "resource.h"

#define APP_TITLE "RomesCov"

#define IDC_PATH            1001
#define IDC_OPEN_KML        1002
#define IDC_OPEN_CSV        1003
#define IDC_FILLIN          1010
#define IDC_GSM             1011
#define IDC_UMTS            1012
#define IDC_LTE             1013
#define IDC_DEFAULT         1014
#define IDC_CALC_TOT        1015
#define IDC_CALC_SEP        1016
#define IDC_AVG_MED         1017
#define IDC_AVG_SMA         1018
#define IDC_DEPTH           1019
#define IDC_MAXSKIP         1020
#define IDC_ANALYSE         1030
#define IDC_SAVE_SETTINGS   1031
#define IDC_PROGRESS        1032
#define IDC_STATUS          1033
#define IDC_LOG             1034
#define IDC_TABLE           1035
#define IDC_TABLE_INFO      1036
#define IDC_REPORT          1037
#define IDC_MAP             1038
#define IDC_ABOUT           1039
#define IDC_LBL_FILE        1100
#define IDC_GRP_SETTINGS    1101
#define IDC_GRP_ACTIONS     1102
#define IDC_LBL_GSM         1103
#define IDC_LBL_UMTS        1104
#define IDC_LBL_LTE         1105
#define IDC_LBL_DEFAULT     1106
#define IDC_LBL_DEPTH       1107
#define IDC_LBL_MAXSKIP     1108
#define IDC_GRP_SMOOTH      1110
#define IDC_LBL_LOG         1109

#define TIMER_POLL          1
#define SETTINGS_HEIGHT     216
#define TOP_ROW_HEIGHT      34
#define LOG_HEIGHT          118

static HINSTANCE  instance;
static HWND       main_window;
static HFONT      ui_font;
static init_t     settings;
static char       source_path[APP_PATH_LEN];
static int        source_chosen = 0;
static char       result_folder[APP_PATH_LEN];   /* where the last run put its files */
static SOURCE_TYPE source_type = SOURCE_KML;
static char       settings_path[APP_PATH_LEN];
static int        shown_log_lines = 0;
static int        table_columns = 0;
static int        last_job_was_import = 0;
static int        auto_analyse = 0;      /* set by the command line: analyse right after the import */

static void start_import(void);
static void start_analysis(void);

/*----Full path of a file next to the exe: settings.ini must not follow the working
     directory, which changes every time a source file is picked.----*/
static void exe_folder_file(const char* file_name, char* path_out)
{
    char* slash;
    GetModuleFileNameA(NULL, path_out, APP_PATH_LEN - 1);
    slash = strrchr(path_out, 92);
    if(slash != NULL) *(slash + 1) = 0;
    else path_out[0] = 0;
    strncat(path_out, file_name, APP_PATH_LEN - strlen(path_out) - 1);
}

static HWND add_control(const char* class_name, const char* text, DWORD style, int id)
{
    HWND control = CreateWindowExA(0, class_name, text, WS_CHILD | WS_VISIBLE | style,
                                   0, 0, 10, 10, main_window, (HMENU)(INT_PTR)id, instance, NULL);
    SendMessage(control, WM_SETFONT, (WPARAM)ui_font, TRUE);
    return control;
}

static void set_text(int id, const char* text)
{
    SetWindowTextA(GetDlgItem(main_window, id), text);
}

static void set_double(int id, double value)
{
    char text[64];
    snprintf(text, sizeof(text), "%.2f", value);
    set_text(id, text);
}

static double get_double(int id, double fallback)
{
    char text[64] = {0};
    GetWindowTextA(GetDlgItem(main_window, id), text, sizeof(text) - 1);
    if(text[0] == 0) return fallback;
    return atof(text);
}

/*=============================== Settings <-> controls =======================*/

static void settings_to_controls(void)
{
    char text[64];

    CheckDlgButton(main_window, IDC_FILLIN, settings.fillinflag ? BST_CHECKED : BST_UNCHECKED);
    set_double(IDC_GSM,     settings.GSMcoveragelvl);
    set_double(IDC_UMTS,    settings.UMTScoveragelvl);
    set_double(IDC_LTE,     settings.LTEcoveragelvl);
    set_double(IDC_DEFAULT, settings.defaultcovlvl);

    CheckDlgButton(main_window, IDC_CALC_TOT, (settings.covercalctype == total)    ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(main_window, IDC_CALC_SEP, (settings.covercalctype == separate) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(main_window, IDC_AVG_MED,  (settings.avgtype == median) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(main_window, IDC_AVG_SMA,  (settings.avgtype == sma)    ? BST_CHECKED : BST_UNCHECKED);

    snprintf(text, sizeof(text), "%d", settings.avgdepth);
    set_text(IDC_DEPTH, text);
    set_double(IDC_MAXSKIP, settings.maxskipdist);
}

/*----Reads the controls back and clamps everything the console build clamps too.----*/
static void controls_to_settings(void)
{
    char text[64] = {0};

    settings.fillinflag      = (IsDlgButtonChecked(main_window, IDC_FILLIN) == BST_CHECKED) ? 1 : 0;
    settings.GSMcoveragelvl  = get_double(IDC_GSM,     settings.GSMcoveragelvl);
    settings.UMTScoveragelvl = get_double(IDC_UMTS,    settings.UMTScoveragelvl);
    settings.LTEcoveragelvl  = get_double(IDC_LTE,     settings.LTEcoveragelvl);
    settings.defaultcovlvl   = get_double(IDC_DEFAULT, settings.defaultcovlvl);
    settings.covercalctype   = (IsDlgButtonChecked(main_window, IDC_CALC_SEP) == BST_CHECKED) ? separate : total;
    settings.avgtype         = (IsDlgButtonChecked(main_window, IDC_AVG_SMA) == BST_CHECKED) ? sma : median;

    GetWindowTextA(GetDlgItem(main_window, IDC_DEPTH), text, sizeof(text) - 1);
    settings.avgdepth = atoi(text);
    if(settings.avgdepth > 100) settings.avgdepth = 100;
    if(settings.avgdepth < 2)   settings.avgdepth = 2;

    settings.maxskipdist = get_double(IDC_MAXSKIP, settings.maxskipdist);
    if(settings.maxskipdist < 0.0) settings.maxskipdist = 0.0;

    settings_to_controls();   /* show the clamped values back to the user */
}

/*================================== File dialog ==============================*/

static void choose_source(SOURCE_TYPE type)
{
    OPENFILENAMEA ofn;
    char          path[APP_PATH_LEN] = {0};
    const char*   filter = (type == SOURCE_KML)
                         ? "KML-файлы (*.kml)\0*.kml\0Все файлы\0*.*\0"
                         : "CSV-файлы (*.csv)\0*.csv\0Все файлы\0*.*\0";

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = main_window;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = sizeof(path);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if(!GetOpenFileNameA(&ofn)) return;

    strncpy(source_path, path, APP_PATH_LEN - 1);
    source_path[APP_PATH_LEN - 1] = 0;
    source_chosen = 1;
    source_type   = type;
    set_text(IDC_PATH, source_path);
    start_import();                  /* the file is converted and shown right away */
}

/*==================================== Table ==================================*/

/* The table shows what the measurement actually is: the four values every folder shares
   (row number, timestamp and the position from the track) once, and then one column of
   signal level per folder.  The GPS folder gets no column of its own - it carries no
   level, and its coordinates are already the third and fourth columns. */
#define TABLE_FIXED_COLUMNS  4

static int table_level_folder[MAX_FOLDERS];   /* level column -> index of its folder */
static int table_level_folder[MAX_FOLDERS];   /* level column -> index of its folder */
static int table_level_count = 0;
static int table_track_folder = 0;            /* folder the shared columns are read from */
static int folder_selected[MAX_FOLDERS];      /* ticked in the header, used by the analysis */

static void table_clear_columns(HWND table)
{
    while(table_columns > 0)
    {
        SendMessage(table, LVM_DELETECOLUMN, 0, 0);
        table_columns--;
    }
}

static void update_selection_info(void)
{
    char info[220];
    int  chosen = 0;
    int  i;

    for(i = 0; i < table_level_count; i++)
        if(folder_selected[table_level_folder[i]]) chosen++;

    if(!app_result_ready())
        snprintf(info, sizeof(info), "Таблица данных: файл не открыт");
    else
        snprintf(info, sizeof(info), "Таблица данных: %u строк; для расчета выбрано элементов: %d из %d",
                 app_result_rows(), chosen, table_level_count);
    set_text(IDC_TABLE_INFO, info);
}

/*----The tick box lives in the column header, so the folder is chosen right where its
     levels are shown.  It needs HDS_CHECKBOXES on the header control itself.----*/
static void table_mark_column(HWND header, int column, int checked)
{
    HDITEMA item;

    memset(&item, 0, sizeof(item));
    item.mask = HDI_FORMAT;
    SendMessage(header, HDM_GETITEMA, column, (LPARAM)&item);
    item.fmt |= HDF_CHECKBOX;
    if(checked) item.fmt |= HDF_CHECKED;
    else        item.fmt &= ~HDF_CHECKED;
    SendMessage(header, HDM_SETITEMA, column, (LPARAM)&item);
}

static void table_toggle_column(int column)
{
    HWND    table  = GetDlgItem(main_window, IDC_TABLE);
    HWND    header = (HWND)SendMessage(table, LVM_GETHEADER, 0, 0);
    HDITEMA item;
    int     level = column - TABLE_FIXED_COLUMNS;
    int     checked;

    if((level < 0) || (level >= table_level_count)) return;

    memset(&item, 0, sizeof(item));
    item.mask = HDI_FORMAT;
    SendMessage(header, HDM_GETITEMA, column, (LPARAM)&item);
    checked = (item.fmt & HDF_CHECKED) ? 0 : 1;      /* the click inverts the box */
    table_mark_column(header, column, checked);

    folder_selected[table_level_folder[level]] = checked;
    update_selection_info();
}

static void table_build(void)
{
    static const char* fixed_titles[TABLE_FIXED_COLUMNS] = {"N", "time", "lat", "lon"};
    static const int   fixed_widths[TABLE_FIXED_COLUMNS] = {60, 100, 115, 115};

    HWND      table = GetDlgItem(main_window, IDC_TABLE);
    HWND      header;
    LVCOLUMNA column;
    int       folders = app_result_folders();
    int       track = app_result_gps_folder();
    int       i;

    SendMessage(table, WM_SETREDRAW, FALSE, 0);
    SendMessage(table, LVM_SETITEMCOUNT, 0, 0);
    table_clear_columns(table);

    if(track < 0) track = 0;      /* a source without a GPS folder: the first one carries the track */
    table_track_folder = track;
    table_level_count  = 0;

    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    for(i = 0; i < TABLE_FIXED_COLUMNS; i++)
    {
        column.iSubItem = i;
        column.cx       = fixed_widths[i];
        column.pszText  = (char*)fixed_titles[i];
        SendMessage(table, LVM_INSERTCOLUMNA, i, (LPARAM)&column);
        table_columns++;
    }

    for(i = 0; i < folders; i++)
    {
        if(i == track) continue;
        column.iSubItem = table_columns;
        column.cx       = 300;
        column.pszText  = (char*)app_result_folder_fullname(i);   /* the full name, as in the file */
        SendMessage(table, LVM_INSERTCOLUMNA, table_columns, (LPARAM)&column);
        table_level_folder[table_level_count] = i;
        folder_selected[i] = 1;                                   /* everything is on by default */
        table_level_count++;
        table_columns++;
    }

    header = (HWND)SendMessage(table, LVM_GETHEADER, 0, 0);
    SetWindowLongPtr(header, GWL_STYLE, GetWindowLongPtr(header, GWL_STYLE) | HDS_CHECKBOXES);
    for(i = 0; i < table_level_count; i++)
        table_mark_column(header, TABLE_FIXED_COLUMNS + i, 1);

    SendMessage(table, LVM_SETITEMCOUNT, (WPARAM)app_result_rows(), 0);
    SendMessage(table, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(table, NULL, TRUE);

    update_selection_info();
}
/*----The list view is virtual: it asks for a cell only when it needs to draw it.----*/
static void table_supply_cell(NMLVDISPINFOA* info)
{
    static char cell[256];
    int         column = info->item.iSubItem;
    unsigned    row = (unsigned)info->item.iItem;

    if((info->item.mask & LVIF_TEXT) == 0) return;

    cell[0] = 0;
    switch(column)
    {
    case 0: snprintf(cell, sizeof(cell), "%d", info->item.iItem + 1);      break;
    case 1: app_result_cell(row, table_track_folder, 2, cell, sizeof(cell)); break;   /* timestamp */
    case 2: app_result_cell(row, table_track_folder, 4, cell, sizeof(cell)); break;   /* latitude  */
    case 3: app_result_cell(row, table_track_folder, 5, cell, sizeof(cell)); break;   /* longitude */
    default:
    {
        int level = column - TABLE_FIXED_COLUMNS;
        if((level >= 0) && (level < table_level_count))
            app_result_cell(row, table_level_folder[level], 3, cell, sizeof(cell));   /* signallevel */
    }
    break;
    }

    info->item.pszText = cell;
}

/*================================ Log and status =============================*/

static void log_append_new_lines(void)
{
    HWND edit  = GetDlgItem(main_window, IDC_LOG);
    int  count = app_log_count();
    int  i;

    for(i = shown_log_lines; i < count; i++)
    {
        char line[APP_LOG_WIDTH + 4];
        snprintf(line, sizeof(line), "%s\r\n", app_log_line(i));
        SendMessageA(edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageA(edit, EM_REPLACESEL, FALSE, (LPARAM)line);
    }
    if(count != shown_log_lines)
    {
        SendMessage(edit, EM_SCROLLCARET, 0, 0);
        shown_log_lines = count;
    }
}

static void log_reset(void)
{
    shown_log_lines = 0;
    set_text(IDC_LOG, "");
}

static void update_progress(void)
{
    HWND progress = GetDlgItem(main_window, IDC_PROGRESS);
    SendMessage(progress, PBM_SETPOS, (WPARAM)(int)(app_job_progress() * 100.0f), 0);
    set_text(IDC_STATUS, app_job_stage());
}

/*----Step one, started right after a file is picked: the source is read (a kml is turned
     into a csv on the way) and the table appears.  No reports are written yet.----*/
static void start_import(void)
{
    if(app_job_state() == JOB_RUNNING)
    {
        set_text(IDC_STATUS, "Обработка уже идёт, дождитесь завершения");
        return;
    }

    controls_to_settings();          /* fillinflag matters while writing the csv */
    log_reset();
    table_clear_columns(GetDlgItem(main_window, IDC_TABLE));
    SendMessage(GetDlgItem(main_window, IDC_TABLE), LVM_SETITEMCOUNT, 0, 0);
    table_level_count = 0;
    set_text(IDC_TABLE_INFO, "Таблица данных: чтение файла...");
    EnableWindow(GetDlgItem(main_window, IDC_ANALYSE), FALSE);
    EnableWindow(GetDlgItem(main_window, IDC_REPORT), FALSE);
    EnableWindow(GetDlgItem(main_window, IDC_MAP), FALSE);

    last_job_was_import = 1;
    app_job_import(source_path, source_type, main_window);
}

/*----Where the results go.  The dialog opens in the folder of the source file, or in the
     one picked the last time, and the answer is kept for the next run.----*/
static int CALLBACK folder_dialog_proc(HWND dialog, UINT message, LPARAM lparam, LPARAM data)
{
    (void)lparam;
    if(message == BFFM_INITIALIZED) SendMessageA(dialog, BFFM_SETSELECTIONA, TRUE, data);
    return 0;
}

/*----Cuts the file name off a path, leaving the folder.----*/
static void folder_of(const char* full_path, char* folder, int size)
{
    char* slash;
    int   length = (int)strlen(full_path);

    if(length > size - 1) length = size - 1;
    memcpy(folder, full_path, (size_t)length);
    folder[length] = 0;

    slash = strrchr(folder, 92);
    if(slash != NULL) *slash = 0;
    else strcpy(folder, ".");
}

static int choose_result_folder(void)
{
    BROWSEINFOA  browse;
    LPITEMIDLIST chosen;
    char         picked[MAX_PATH] = {0};
    char         start[APP_PATH_LEN];

    if(result_folder[0] != 0)
    {
        strncpy(start, result_folder, sizeof(start) - 1);
        start[sizeof(start) - 1] = 0;
    }
    else folder_of(source_path, start, sizeof(start));

    memset(&browse, 0, sizeof(browse));
    browse.hwndOwner = main_window;
    browse.lpszTitle = "Папка для результатов расчёта";
    browse.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    browse.lpfn      = folder_dialog_proc;
    browse.lParam    = (LPARAM)start;

    chosen = SHBrowseForFolderA(&browse);
    if(chosen == NULL) return 0;
    if(SHGetPathFromIDListA(chosen, picked))
    {
        strncpy(result_folder, picked, sizeof(result_folder) - 1);
        result_folder[sizeof(result_folder) - 1] = 0;
    }
    CoTaskMemFree(chosen);
    return (result_folder[0] != 0);
}

/*----Step two: coverage for the elements ticked in the table header.  Opening a file
     writes nothing any more, so this is where the folder for the results is asked for.----*/
static void start_analysis(void)
{
    int i;
    int chosen = 0;

    if(app_job_state() == JOB_RUNNING)
    {
        set_text(IDC_STATUS, "Обработка уже идёт, дождитесь завершения");
        return;
    }
    if(!app_result_ready())
    {
        MessageBoxA(main_window, "Сначала откройте kml- или csv-файл.", APP_TITLE,
                    MB_ICONINFORMATION | MB_OK);
        return;
    }

    for(i = 0; i < table_level_count; i++)
        if(folder_selected[table_level_folder[i]]) chosen++;
    if(chosen == 0)
    {
        MessageBoxA(main_window, "Отметьте галочками хотя бы один элемент в заголовке таблицы.",
                    APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }

    /* A run started from the command line must not stop on a dialog: it saves the results
       next to the source file. */
    if(auto_analyse) folder_of(source_path, result_folder, sizeof(result_folder));
    else if(!choose_result_folder())
    {
        set_text(IDC_STATUS, "Расчёт отменён: папка для результатов не выбрана");
        return;
    }

    controls_to_settings();
    log_reset();
    EnableWindow(GetDlgItem(main_window, IDC_ANALYSE), FALSE);
    EnableWindow(GetDlgItem(main_window, IDC_REPORT), FALSE);
    EnableWindow(GetDlgItem(main_window, IDC_MAP), FALSE);
    last_job_was_import = 0;
    app_job_analyse(&settings, folder_selected, result_folder, main_window);
}

/*----The report is written by the run itself, into the same folder as the tables; the
     button only opens it in the browser.----*/
static void open_report(void)
{
    const char* path = app_result_report_path();

    if(path == NULL)
    {
        MessageBoxA(main_window, "Сначала выполните расчёт покрытия - отчёт строится по его результатам.",
                    APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }

    set_text(IDC_STATUS, "Открываю отчёт в браузере");
    ShellExecuteA(main_window, "open", path, NULL, NULL, SW_SHOWNORMAL);
}

/*----The status line after a run: the folder is what the user needs to see.----*/
static void set_status_saved(void)
{
    char text[APP_PATH_LEN + 64];

    snprintf(text, sizeof(text), "Расчёт готов, файлы сохранены в %s", result_folder);
    set_text(IDC_STATUS, text);
}

static void job_finished(void)
{
    app_job_join();
    log_append_new_lines();
    update_progress();

    if(app_job_state() == JOB_DONE)
    {
        if(last_job_was_import)
        {
            table_build();
            EnableWindow(GetDlgItem(main_window, IDC_ANALYSE), TRUE);
            set_text(IDC_STATUS, "Файл прочитан. Отметьте элементы и нажмите расчёт покрытия");
            if(auto_analyse) { start_analysis(); auto_analyse = 0; }   /* command line asked for the full run */
        }
        else
        {
            EnableWindow(GetDlgItem(main_window, IDC_ANALYSE), TRUE);
            update_selection_info();
            EnableWindow(GetDlgItem(main_window, IDC_REPORT), TRUE);
            EnableWindow(GetDlgItem(main_window, IDC_MAP),
                         (settings.covercalctype == total) ? TRUE : FALSE);
            map_view_refresh();          /* the map, if open, shows the fresh coverage */
            set_status_saved();
        }
    }
    else
    {
        EnableWindow(GetDlgItem(main_window, IDC_ANALYSE), app_result_ready() ? TRUE : FALSE);
        if(last_job_was_import) set_text(IDC_TABLE_INFO, "Таблица данных: файл не открыт");
        set_text(IDC_STATUS, "Ошибка обработки, подробности в логе");
    }
}

/*----Notifications of the header belong to the list view, not to this window, so the
     list view is subclassed to catch the click on a tick box.----*/
static LRESULT CALLBACK table_subclass(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                       UINT_PTR id, DWORD_PTR reference)
{
    (void)id; (void)reference;

    if(message == WM_NOTIFY)
    {
        NMHEADERA* header_note = (NMHEADERA*)lparam;
        if(header_note->hdr.code == HDN_ITEMSTATEICONCLICK)   /* MinGW knows no A/W flavours here */
        {
            table_toggle_column(header_note->iItem);
            return 0;
        }
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

/*=========================== Controls and layout =============================*/

static void create_controls(void)
{
    add_control("STATIC", "Исходный файл:", SS_RIGHT | SS_CENTERIMAGE, IDC_LBL_FILE);
    add_control("EDIT",   "файл не выбран", ES_LEFT | ES_AUTOHSCROLL | ES_READONLY | WS_BORDER, IDC_PATH);
    add_control("BUTTON", "Открыть kml...", BS_PUSHBUTTON | WS_TABSTOP, IDC_OPEN_KML);
    add_control("BUTTON", "Открыть csv...", BS_PUSHBUTTON | WS_TABSTOP, IDC_OPEN_CSV);

    add_control("BUTTON", "Параметры расчёта", BS_GROUPBOX, IDC_GRP_SETTINGS);
    add_control("BUTTON", "Автозаполнение пустых точек (filflag)", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_FILLIN);

    add_control("STATIC", "Порог GSM, dBm:",     SS_RIGHT | SS_CENTERIMAGE, IDC_LBL_GSM);
    add_control("EDIT",   "", ES_LEFT | WS_BORDER | WS_TABSTOP, IDC_GSM);
    add_control("STATIC", "Порог UMTS, dBm:",    SS_RIGHT | SS_CENTERIMAGE, IDC_LBL_UMTS);
    add_control("EDIT",   "", ES_LEFT | WS_BORDER | WS_TABSTOP, IDC_UMTS);
    add_control("STATIC", "Порог LTE, dBm:",     SS_RIGHT | SS_CENTERIMAGE, IDC_LBL_LTE);
    add_control("EDIT",   "", ES_LEFT | WS_BORDER | WS_TABSTOP, IDC_LTE);
    add_control("STATIC", "Порог default, dBm:", SS_RIGHT | SS_CENTERIMAGE, IDC_LBL_DEFAULT);
    add_control("EDIT",   "", ES_LEFT | WS_BORDER | WS_TABSTOP, IDC_DEFAULT);

    add_control("BUTTON", "Общий расчёт (TOT)",       BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, IDC_CALC_TOT);
    add_control("BUTTON", "По элементам (SEP)",          BS_AUTORADIOBUTTON, IDC_CALC_SEP);

    add_control("BUTTON", "Сглаживание", BS_GROUPBOX | WS_GROUP, IDC_GRP_SMOOTH);
    add_control("BUTTON", "Медиана (MED)",            BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, IDC_AVG_MED);
    add_control("BUTTON", "Скользящее среднее (SMA)", BS_AUTORADIOBUTTON, IDC_AVG_SMA);

    add_control("STATIC", "Точек усреднения уровня:",   SS_RIGHT | SS_CENTERIMAGE, IDC_LBL_DEPTH);
    add_control("EDIT",   "", ES_LEFT | ES_NUMBER | WS_BORDER | WS_TABSTOP, IDC_DEPTH);
    add_control("STATIC", "Окно сглаживания, км:", SS_RIGHT | SS_CENTERIMAGE, IDC_LBL_MAXSKIP);
    add_control("EDIT",   "", ES_LEFT | WS_BORDER | WS_TABSTOP, IDC_MAXSKIP);

    add_control("BUTTON", "Обработка", BS_GROUPBOX, IDC_GRP_ACTIONS);
    add_control("BUTTON", "Рассчитать покрытие", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_ANALYSE);
    add_control("BUTTON", "Открыть HTML-отчёт", BS_PUSHBUTTON | WS_TABSTOP, IDC_REPORT);
    add_control("BUTTON", "Карта покрытия", BS_PUSHBUTTON | WS_TABSTOP, IDC_MAP);
    add_control("BUTTON", "Сохранить настройки", BS_PUSHBUTTON | WS_TABSTOP, IDC_SAVE_SETTINGS);
    add_control("BUTTON", "О программе", BS_PUSHBUTTON | WS_TABSTOP, IDC_ABOUT);
    add_control(PROGRESS_CLASSA, "", 0, IDC_PROGRESS);
    add_control("STATIC", "", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, IDC_STATUS);

    add_control("STATIC", "Ход работы:", SS_LEFT | SS_CENTERIMAGE, IDC_LBL_LOG);
    add_control("EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, IDC_LOG);
    add_control("STATIC", "Таблица данных: нет данных", SS_LEFT | SS_CENTERIMAGE, IDC_TABLE_INFO);

    add_control(WC_LISTVIEWA, "", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, IDC_TABLE);
    SendMessage(GetDlgItem(main_window, IDC_TABLE), LVM_SETEXTENDEDLISTVIEWSTYLE,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(GetDlgItem(main_window, IDC_TABLE), table_subclass, 1, 0);   /* to catch the header tick boxes */
}

static void place(int id, int x, int y, int width, int height)
{
    HWND control = GetDlgItem(main_window, id);
    if(control != NULL) MoveWindow(control, x, y, width, height, TRUE);
}

/*----The settings block keeps its size; the log and the table take what is left.----*/
static void layout(int width, int height)
{
    const int left = 12;
    const int settings_width = 640;
    int actions_x = left + settings_width + 12;
    int actions_width = width - actions_x - left;
    int half_button;
    int y_block = 12 + TOP_ROW_HEIGHT;
    int y_log, y_table, row;

    if(actions_width < 250) actions_width = 250;
    half_button = (actions_width - 40) / 2;

    place(IDC_LBL_FILE, left, 12, 104, 24);
    place(IDC_PATH,     left + 110, 12, width - left - 110 - 248, 24);
    place(IDC_OPEN_KML, width - 244, 11, 112, 26);
    place(IDC_OPEN_CSV, width - 126, 11, 112, 26);

    place(IDC_GRP_SETTINGS, left, y_block, settings_width, SETTINGS_HEIGHT);
    place(IDC_GRP_ACTIONS,  actions_x, y_block, actions_width, SETTINGS_HEIGHT);

    row = y_block + 22;
    place(IDC_FILLIN, left + 16, row, 400, 22);
    row += 26;
    place(IDC_LBL_GSM,     left + 16,  row, 130, 22);
    place(IDC_GSM,         left + 152, row, 80,  22);
    place(IDC_LBL_UMTS,    left + 260, row, 140, 22);
    place(IDC_UMTS,        left + 406, row, 80,  22);
    row += 26;
    place(IDC_LBL_LTE,     left + 16,  row, 130, 22);
    place(IDC_LTE,         left + 152, row, 80,  22);
    place(IDC_LBL_DEFAULT, left + 260, row, 140, 22);
    place(IDC_DEFAULT,     left + 406, row, 80,  22);
    row += 28;
    place(IDC_CALC_TOT, left + 16,  row, 200, 22);
    place(IDC_CALC_SEP, left + 260, row, 200, 22);

    row += 30;                                            /* frame of the smoothing block */
    place(IDC_GRP_SMOOTH,  left + 8,   row, settings_width - 16, 78);
    place(IDC_AVG_MED,     left + 16,  row + 20, 200, 22);
    place(IDC_AVG_SMA,     left + 260, row + 20, 240, 22);
    place(IDC_LBL_DEPTH,   left + 16,  row + 46, 168, 22);
    place(IDC_DEPTH,       left + 188, row + 46, 60,  22);
    place(IDC_LBL_MAXSKIP, left + 256, row + 46, 156, 22);
    place(IDC_MAXSKIP,     left + 416, row + 46, 70,  22);

    place(IDC_ANALYSE,       actions_x + 16, y_block + 20,  actions_width - 32, 34);
    place(IDC_REPORT,        actions_x + 16, y_block + 58,  actions_width - 32, 28);
    place(IDC_MAP,           actions_x + 16, y_block + 90,  actions_width - 32, 28);
    place(IDC_SAVE_SETTINGS, actions_x + 16, y_block + 122, half_button, 26);
    place(IDC_ABOUT,         actions_x + 24 + half_button, y_block + 122, half_button, 26);
    place(IDC_PROGRESS,      actions_x + 16, y_block + SETTINGS_HEIGHT - 62, actions_width - 32, 16);
    place(IDC_STATUS,        actions_x + 16, y_block + SETTINGS_HEIGHT - 42, actions_width - 32, 32);

    y_log = y_block + SETTINGS_HEIGHT + 10;
    place(IDC_LBL_LOG, left, y_log, 200, 18);
    place(IDC_LOG, left, y_log + 20, width - 2 * left, LOG_HEIGHT);

    y_table = y_log + 20 + LOG_HEIGHT + 10;
    place(IDC_TABLE_INFO, left, y_table, width - 2 * left, 20);
    place(IDC_TABLE, left, y_table + 22, width - 2 * left, height - y_table - 34);
}

/*================================ Window procedure ===========================*/

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch(message)
    {
    case WM_CREATE:
        main_window = window;
        create_controls();
        settings_to_controls();
        EnableWindow(GetDlgItem(window, IDC_ANALYSE), FALSE);
        EnableWindow(GetDlgItem(window, IDC_REPORT), FALSE);
        EnableWindow(GetDlgItem(window, IDC_MAP), FALSE);
        SetTimer(window, TIMER_POLL, 100, NULL);
        return 0;

    case WM_SIZE:
        layout(LOWORD(lparam), HIWORD(lparam));
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lparam)->ptMinTrackSize.x = 1010;
        ((MINMAXINFO*)lparam)->ptMinTrackSize.y = 640;
        return 0;

    case WM_COMMAND:
        switch(LOWORD(wparam))
        {
        case IDC_OPEN_KML: choose_source(SOURCE_KML); return 0;
        case IDC_OPEN_CSV: choose_source(SOURCE_CSV); return 0;
        case IDC_ANALYSE:  start_analysis();          return 0;
        case IDC_REPORT:   open_report();             return 0;
        case IDC_MAP:      map_view_open(instance, main_window); return 0;
        case IDC_ABOUT:    about_show(instance, main_window);     return 0;
        case IDC_SAVE_SETTINGS:
            controls_to_settings();
            if(app_settings_save(settings_path, &settings))
                set_text(IDC_STATUS, "Настройки записаны в settings.ini");
            else
                set_text(IDC_STATUS, "Не удалось записать settings.ini");
            return 0;
        default: break;
        }
        break;

    case WM_NOTIFY:
        if(((NMHDR*)lparam)->idFrom == IDC_TABLE)
        {
            if(((NMHDR*)lparam)->code == LVN_GETDISPINFOA)
            {
                table_supply_cell((NMLVDISPINFOA*)lparam);
                return 0;
            }
        }
        break;

    case WM_TIMER:
        if(wparam == TIMER_POLL)
        {
            log_append_new_lines();
            if(app_job_state() == JOB_RUNNING) update_progress();
        }
        return 0;

    case WM_APP_JOB_FINISHED:
        job_finished();
        return 0;

    case WM_CLOSE:
        if(app_job_state() == JOB_RUNNING)
        {
            if(MessageBoxA(window, "Идёт обработка файла. Закрыть программу?", APP_TITLE,
                           MB_ICONQUESTION | MB_YESNO) != IDYES) return 0;
        }
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(window, TIMER_POLL);
        PostQuitMessage(0);
        return 0;

    default: break;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

/*===================================== main ==================================*/

static void create_ui_font(void)
{
    NONCLIENTMETRICSA metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if(SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        ui_font = CreateFontIndirectA(&metrics.lfMessageFont);
    if(ui_font == NULL) ui_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

int main(int argc, char** argv)
{
    WNDCLASSEXA  window_class;
    INITCOMMONCONTROLSEX controls;
    MSG          message;



    SetProcessDPIAware();   /* without it the window is scaled and blurry above 100% */
    OleInitialize(NULL);    /* the folder dialog wants the new look, and that needs COM */

    instance = GetModuleHandleA(NULL);
    memset(&controls, 0, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC  = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    create_ui_font();
    app_log_init();
    exe_folder_file("settings.ini", settings_path);
    app_settings_load(settings_path, &settings);

    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize        = sizeof(window_class);
    window_class.lpfnWndProc   = window_proc;
    window_class.hInstance     = instance;
    window_class.hCursor       = LoadCursor(NULL, IDC_ARROW);
    window_class.hIcon         = LoadIconA(instance, MAKEINTRESOURCEA(IDI_APPICON));
    window_class.hIconSm       = LoadIconA(instance, MAKEINTRESOURCEA(IDI_APPICON));
    window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    window_class.lpszClassName = "RomesCovWindow";
    if(!RegisterClassExA(&window_class))
    {
        MessageBoxA(NULL, "Не удалось зарегистрировать класс окна", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    main_window = CreateWindowExA(0, window_class.lpszClassName, APP_TITLE,
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
                                 NULL, NULL, instance, NULL);
    if(main_window == NULL)
    {
        MessageBoxA(NULL, "Не удалось создать окно", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    set_text(IDC_STATUS, "Выберите kml- или csv-файл");
    /* A path on the command line preselects the file, so the exe can be used as a target
       for "Open with" or a drag and drop; "--run" additionally starts the conversion. */
    if(argc > 1)
    {
        const char* extension = strrchr(argv[1], 46);
        strncpy(source_path, argv[1], APP_PATH_LEN - 1);
        source_path[APP_PATH_LEN - 1] = 0;
        source_chosen = 1;
        source_type = ((extension != NULL) && (_stricmp(extension, ".csv") == 0)) ? SOURCE_CSV : SOURCE_KML;
        set_text(IDC_PATH, source_path);
        if((argc > 2) && (strcmp(argv[2], "--run") == 0)) auto_analyse = 1;
        start_import();                 /* a picked file is read at once, as in the window */
    }

    ShowWindow(main_window, SW_SHOWNORMAL);
    UpdateWindow(main_window);

    while(GetMessage(&message, NULL, 0, 0) > 0)
    {
        /* keys of the map window must not be swallowed by the dialog navigation */
        HWND root = GetAncestor(message.hwnd, GA_ROOT);
        if((root == map_view_hwnd()) || (root == about_hwnd())
           || !IsDialogMessage(main_window, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }

    app_job_join();
    return 0;
}
