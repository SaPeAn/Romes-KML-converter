/*
 *  map_view - the coverage map window.
 *
 *  The projection is the simplest one that keeps a track undistorted: degrees of latitude
 *  go straight to pixels, degrees of longitude are squeezed by the cosine of the latitude
 *  in the centre of the view.  Over a few hundred kilometres this is indistinguishable
 *  from a proper projection and costs two multiplications.
 *
 *  The track and both sets of coverage flags are copied out of app_core when the window
 *  opens, so the map keeps its picture while the main window starts another job instead
 *  of holding a pointer into data that job is about to free.
 *
 *  This file is windows-1251, like the rest of the gui.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "app_core.h"
#include "map_view.h"
#include "resource.h"

#define MAP_CLASS_NAME   "RomesCovMapWindow"
#define STATUS_HEIGHT    26
#define METRES_IN_DEGREE 111194.9          /* one degree of latitude on a sphere of 6371 km */
#define PI_VALUE         3.14159265358979323846
#define SCALE_MIN        0.15              /* pixels in a degree: the whole world at once */
#define SCALE_MAX        6000000.0         /* about two centimetres in a pixel */
#define LINE_HEIGHT      18
#define AVG_OFFSET       8.0               /* на сколько пикселей сглаженная линия уходит вбок */

#define LAYER_TRACK      0
#define LAYER_RAW_GAP    1
#define LAYER_AVG_GAP    2

typedef struct {
    double        latitude;
    double        longitude;
    unsigned char raw;                     /* 1 - covered before the smoothing by distance */
    unsigned char avg;                     /* 1 - covered after it */
} map_point_t;

static HINSTANCE    map_instance;
static HWND         map_window;
static HFONT        map_font;

/*----The copy of the results----*/
static map_point_t* points;
static int          point_count;
static char         source_name[300];
static double       track_length;
static double       raw_uncovered;
static double       avg_uncovered;
static double       smooth_window;

/*----Where the map looks----*/
static double       view_latitude;         /* geographical centre of the map area */
static double       view_longitude;
static double       view_scale;            /* pixels in one degree of latitude */
static double       view_squeeze;          /* cosine of the centre latitude */

/*----Mouse----*/
static int          drag_active;
static POINT        drag_start;
static double       drag_latitude;
static double       drag_longitude;
static int          cursor_known;
static POINT        cursor_position;

/*----The map is drawn into a bitmap and redrawn only when the view changes; the legend,
     the scale bar and the status line are painted over it on every WM_PAINT.----*/
static HDC          cache_dc;
static HBITMAP      cache_bitmap;
static HBITMAP      cache_old_bitmap;
static int          cache_width;
static int          cache_height;
static int          cache_valid;

/*----Device coordinates of every point, recalculated together with the cache----*/
static double*      device_x;
static double*      device_y;

/*----Те же точки, сдвинутые вбок: по ним рисуется сглаженное покрытие----*/
static double*      offset_x;
static double*      offset_y;

/*============================ Data taken from app_core =======================*/

static void release_points(void)
{
    free(points);   points   = NULL;
    free(device_x); device_x = NULL;
    free(device_y); device_y = NULL;
    free(offset_x); offset_x = NULL;
    free(offset_y); offset_y = NULL;
    point_count = 0;
}

/*----Copies the track and both coverage flags.  Returns 0 when there is nothing to show:
     no analysis yet, or an analysis by elements, which leaves no smoothed track.----*/
static int collect_points(void)
{
    const folder_t* track;
    const folder_t* averaged;
    const init_t*   settings;
    int             gps;
    int             folders;
    unsigned        j;

    if(!app_result_analysed()) return 0;

    settings = app_result_settings();
    if((settings == NULL) || (settings->covercalctype != total)) return 0;

    folders = app_result_folders();
    gps     = app_result_gps_folder();
    if((gps < 0) || (folders <= 0)) return 0;

    track    = app_result_folder(gps);
    averaged = app_result_folder(folders);          /* the reserved slot */
    if((track == NULL) || (averaged == NULL)) return 0;
    if((track->placemark_arr == NULL) || (averaged->placemark_arr == NULL)) return 0;
    if(track->placemark_quantity < 2) return 0;

    release_points();
    points   = (map_point_t*)malloc(sizeof(map_point_t) * track->placemark_quantity);
    device_x = (double*)malloc(sizeof(double) * track->placemark_quantity);
    device_y = (double*)malloc(sizeof(double) * track->placemark_quantity);
    offset_x = (double*)malloc(sizeof(double) * track->placemark_quantity);
    offset_y = (double*)malloc(sizeof(double) * track->placemark_quantity);
    if((points == NULL) || (device_x == NULL) || (device_y == NULL)
       || (offset_x == NULL) || (offset_y == NULL))
    {
        release_points();
        return 0;
    }

    for(j = 0; j < track->placemark_quantity; j++)
    {
        points[j].latitude  = track->placemark_arr[j].latitude;
        points[j].longitude = track->placemark_arr[j].longitude;
        points[j].raw       = (unsigned char)(track->placemark_arr[j].coverfl ? 1 : 0);
        points[j].avg       = (unsigned char)(averaged->placemark_arr[j].coverfl ? 1 : 0);
    }
    point_count = (int)track->placemark_quantity;

    track_length  = averaged->totdist;
    raw_uncovered = track->uncovtotdist;
    avg_uncovered = averaged->uncovtotdist;
    smooth_window = settings->maxskipdist;
    strncpy(source_name, app_result_source_name(), sizeof(source_name) - 1);
    source_name[sizeof(source_name) - 1] = 0;
    return 1;
}

/*=============================== Projection ==================================*/

static void set_centre(double latitude, double longitude)
{
    if(latitude >  85.0) latitude =  85.0;
    if(latitude < -85.0) latitude = -85.0;
    view_latitude  = latitude;
    view_longitude = longitude;
    view_squeeze   = cos(latitude * PI_VALUE / 180.0);
    if(view_squeeze < 0.02) view_squeeze = 0.02;    /* the poles are of no interest here */
}

static void map_area(int* width, int* height)
{
    RECT client;

    GetClientRect(map_window, &client);
    *width  = client.right;
    *height = client.bottom - STATUS_HEIGHT;
    if(*width  < 1) *width  = 1;
    if(*height < 1) *height = 1;
}

static void geo_to_device(double latitude, double longitude, int width, int height,
                          double* x, double* y)
{
    *x = width  / 2.0 + (longitude - view_longitude) * view_squeeze * view_scale;
    *y = height / 2.0 + (view_latitude - latitude) * view_scale;
}

static void device_to_geo(double x, double y, int width, int height,
                          double* latitude, double* longitude)
{
    *longitude = view_longitude + (x - width  / 2.0) / (view_squeeze * view_scale);
    *latitude  = view_latitude  - (y - height / 2.0) / view_scale;
}

/*----Puts the whole track into the window with a margin.----*/
static void fit_track(void)
{
    double min_lat, max_lat, min_lon, max_lon;
    double scale_x, scale_y;
    int    width, height;
    int    j;

    if(point_count < 1) return;
    map_area(&width, &height);

    min_lat = max_lat = points[0].latitude;
    min_lon = max_lon = points[0].longitude;
    for(j = 1; j < point_count; j++)
    {
        if(points[j].latitude  < min_lat) min_lat = points[j].latitude;
        if(points[j].latitude  > max_lat) max_lat = points[j].latitude;
        if(points[j].longitude < min_lon) min_lon = points[j].longitude;
        if(points[j].longitude > max_lon) max_lon = points[j].longitude;
    }

    set_centre((min_lat + max_lat) / 2.0, (min_lon + max_lon) / 2.0);

    scale_y = ((max_lat - min_lat) > 1e-9) ? (height * 0.86) / (max_lat - min_lat) : SCALE_MAX;
    scale_x = ((max_lon - min_lon) > 1e-9) ? (width * 0.86) / ((max_lon - min_lon) * view_squeeze)
                                           : SCALE_MAX;
    view_scale = (scale_x < scale_y) ? scale_x : scale_y;
    if(view_scale > SCALE_MAX) view_scale = SCALE_MAX;
    if(view_scale < SCALE_MIN) view_scale = SCALE_MIN;
    cache_valid = 0;
}

static void zoom_at(int x, int y, double factor)
{
    double latitude, longitude, new_latitude, new_longitude;
    int    width, height;

    map_area(&width, &height);
    device_to_geo(x, y, width, height, &latitude, &longitude);

    view_scale *= factor;
    if(view_scale > SCALE_MAX) view_scale = SCALE_MAX;
    if(view_scale < SCALE_MIN) view_scale = SCALE_MIN;

    /* the point under the cursor has to stay under the cursor */
    new_latitude = latitude + (y - height / 2.0) / view_scale;
    set_centre(new_latitude, view_longitude);
    new_longitude = longitude - (x - width / 2.0) / (view_squeeze * view_scale);
    set_centre(new_latitude, new_longitude);

    cache_valid = 0;
    InvalidateRect(map_window, NULL, FALSE);
}

/*============================== Drawing helpers ==============================*/

/*----Liang-Barsky: cuts the segment down to the visible rectangle.  Without it a segment
     that starts far outside the window would be handed to GDI with coordinates it draws
     unreliably, and deep zoom makes such segments the rule.----*/
static int clip_segment(double* x0, double* y0, double* x1, double* y1,
                        double left, double top, double right, double bottom)
{
    double dx = *x1 - *x0;
    double dy = *y1 - *y0;
    double t0 = 0.0;
    double t1 = 1.0;
    double p[4], q[4];
    int    i;

    p[0] = -dx; q[0] = *x0 - left;
    p[1] =  dx; q[1] = right - *x0;
    p[2] = -dy; q[2] = *y0 - top;
    p[3] =  dy; q[3] = bottom - *y0;

    for(i = 0; i < 4; i++)
    {
        if(p[i] == 0.0)
        {
            if(q[i] < 0.0) return 0;                /* parallel and outside */
        }
        else
        {
            double t = q[i] / p[i];
            if(p[i] < 0.0) { if(t > t1) return 0; if(t > t0) t0 = t; }
            else           { if(t < t0) return 0; if(t < t1) t1 = t; }
        }
    }

    *x1 = *x0 + t1 * dx;
    *y1 = *y0 + t1 * dy;
    *x0 = *x0 + t0 * dx;
    *y0 = *y0 + t0 * dy;
    return 1;
}

static void draw_clipped(HDC dc, double x0, double y0, double x1, double y1,
                         int width, int height)
{
    if(!clip_segment(&x0, &y0, &x1, &y1, -4.0, -4.0, width + 4.0, height + 4.0)) return;
    MoveToEx(dc, (int)(x0 + 0.5), (int)(y0 + 0.5), NULL);
    LineTo(dc, (int)(x1 + 0.5), (int)(y1 + 0.5));
}

/*----Is the segment that ends at point j part of this layer?  The distance between two
     points belongs to the coverage region of the earlier one - exactly as createcovtab()
     counts it, so the picture and the totals cannot disagree.----*/
static int segment_in_layer(int layer, int j)
{
    switch(layer)
    {
    case LAYER_RAW_GAP: return (points[j - 1].raw == 0);
    case LAYER_AVG_GAP: return (points[j - 1].avg == 0);
    default:            return 1;
    }
}

/*----Сглаженное покрытие идёт не поверх трека, а рядом с ним: линия сдвигается по нормали
     к направлению трека на несколько пикселей.  Нормаль считается по соседним точкам, а не
     по одному отрезку, поэтому сдвинутая линия остаётся связной на поворотах.  Соседи
     ищутся вширь, пока не разойдутся хотя бы на полпикселя: на дальнем зуме соседние точки
     попадают в один пиксель, и без этого сдвиг начинал бы дёргаться.----*/
static void compute_offsets(void)
{
    int j;

    for(j = 0; j < point_count; j++)
    {
        double dx = 0.0;
        double dy = 0.0;
        double length = 0.0;
        int    step;

        for(step = 1; step <= 16; step++)
        {
            int a = ((j - step) >= 0) ? (j - step) : 0;
            int b = ((j + step) < point_count) ? (j + step) : (point_count - 1);

            dx = device_x[b] - device_x[a];
            dy = device_y[b] - device_y[a];
            length = sqrt(dx * dx + dy * dy);
            if(length > 0.5) break;
        }

        if(length < 1e-9)
        {
            offset_x[j] = device_x[j];
            offset_y[j] = device_y[j];
            continue;
        }

        offset_x[j] = device_x[j] - (dy / length) * AVG_OFFSET;
        offset_y[j] = device_y[j] + (dx / length) * AVG_OFFSET;
    }
}

/*----Draws one layer, skipping points that fall on the pixel the pen already stands on.
     At a world-wide zoom this turns seventeen thousand segments into a few dozen.----*/
static void draw_layer(HDC dc, int layer, int width, int height)
{
    const double* line_x = (layer == LAYER_AVG_GAP) ? offset_x : device_x;
    const double* line_y = (layer == LAYER_AVG_GAP) ? offset_y : device_y;
    double last_x = 0.0;
    double last_y = 0.0;
    int    in_run = 0;
    int    j;

    for(j = 1; j < point_count; j++)
    {
        int run_ends;

        if(!segment_in_layer(layer, j)) { in_run = 0; continue; }

        if(!in_run)
        {
            last_x = line_x[j - 1];
            last_y = line_y[j - 1];
            in_run = 1;
        }

        run_ends = (j == point_count - 1) || !segment_in_layer(layer, j + 1);
        if(run_ends || (fabs(line_x[j] - last_x) >= 1.0) || (fabs(line_y[j] - last_y) >= 1.0))
        {
            draw_clipped(dc, last_x, last_y, line_x[j], line_y[j], width, height);
            last_x = line_x[j];
            last_y = line_y[j];
        }
    }
}

/*=============================== Grid ========================================*/

static const double grid_ladder[] = {
    0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05,
    0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0
};

static double grid_step(double pixels_in_degree)
{
    int count = (int)(sizeof(grid_ladder) / sizeof(grid_ladder[0]));
    int i;

    for(i = 0; i < count; i++)
        if((grid_ladder[i] * pixels_in_degree) >= 90.0) return grid_ladder[i];
    return grid_ladder[count - 1];
}

static void format_degrees(char* text, int size, double value, double step)
{
    if(step >= 1.0)        snprintf(text, size, "%.0f°", value);
    else if(step >= 0.1)   snprintf(text, size, "%.1f°", value);
    else if(step >= 0.01)  snprintf(text, size, "%.2f°", value);
    else if(step >= 0.001) snprintf(text, size, "%.3f°", value);
    else                   snprintf(text, size, "%.4f°", value);
}

static void draw_grid(HDC dc, int width, int height)
{
    HPEN   pen = CreatePen(PS_SOLID, 1, RGB(214, 220, 228));
    HPEN   old_pen = (HPEN)SelectObject(dc, pen);
    double top_lat, bottom_lat, left_lon, right_lon;
    double step_lat, step_lon, value;
    double x, y;
    char   text[64];
    int    guard;

    device_to_geo(0.0, 0.0, width, height, &top_lat, &left_lon);
    device_to_geo(width, height, width, height, &bottom_lat, &right_lon);

    step_lat = grid_step(view_scale);
    step_lon = grid_step(view_scale * view_squeeze);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(120, 130, 145));

    guard = 0;
    for(value = ceil(bottom_lat / step_lat) * step_lat; (value <= top_lat) && (guard < 400);
        value += step_lat, guard++)
    {
        geo_to_device(value, view_longitude, width, height, &x, &y);
        MoveToEx(dc, 0, (int)(y + 0.5), NULL);
        LineTo(dc, width, (int)(y + 0.5));
        format_degrees(text, sizeof(text), value, step_lat);
        TextOutA(dc, 4, (int)y - 15, text, (int)strlen(text));
    }

    guard = 0;
    for(value = ceil(left_lon / step_lon) * step_lon; (value <= right_lon) && (guard < 400);
        value += step_lon, guard++)
    {
        geo_to_device(view_latitude, value, width, height, &x, &y);
        MoveToEx(dc, (int)(x + 0.5), 0, NULL);
        LineTo(dc, (int)(x + 0.5), height);
        format_degrees(text, sizeof(text), value, step_lon);
        TextOutA(dc, (int)x + 4, 2, text, (int)strlen(text));
    }

    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

/*=============================== The map =====================================*/

static void draw_map(HDC dc, int width, int height)
{
    HBRUSH background = CreateSolidBrush(RGB(250, 251, 252));
    RECT   area;
    HPEN   pen;
    HPEN   old_pen;
    int    j;

    area.left = 0; area.top = 0; area.right = width; area.bottom = height;
    FillRect(dc, &area, background);
    DeleteObject(background);

    if(point_count < 2)
    {
        const char* message = "Нет данных для карты: сначала выполните общий расчёт (TOT)";
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(90, 100, 115));
        TextOutA(dc, 20, 20, message, (int)strlen(message));
        return;
    }

    draw_grid(dc, width, height);

    for(j = 0; j < point_count; j++)
        geo_to_device(points[j].latitude, points[j].longitude, width, height,
                      &device_x[j], &device_y[j]);
    compute_offsets();

    pen = CreatePen(PS_SOLID, 3, RGB(40, 140, 90));            /* the track itself */
    old_pen = (HPEN)SelectObject(dc, pen);
    draw_layer(dc, LAYER_TRACK, width, height);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    pen = CreatePen(PS_SOLID, 9, RGB(214, 65, 55));            /* gaps before smoothing */
    old_pen = (HPEN)SelectObject(dc, pen);
    draw_layer(dc, LAYER_RAW_GAP, width, height);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    pen = CreatePen(PS_SOLID, 3, RGB(250, 166, 26));           /* gaps after smoothing */
    old_pen = (HPEN)SelectObject(dc, pen);
    draw_layer(dc, LAYER_AVG_GAP, width, height);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    /* the ends of the track, so it is clear where the drive started */
    if(point_count > 1)
    {
        HBRUSH mark = CreateSolidBrush(RGB(40, 90, 200));
        HBRUSH old_brush = (HBRUSH)SelectObject(dc, mark);
        pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        old_pen = (HPEN)SelectObject(dc, pen);
        Ellipse(dc, (int)device_x[0] - 5, (int)device_y[0] - 5,
                    (int)device_x[0] + 5, (int)device_y[0] + 5);
        Ellipse(dc, (int)device_x[point_count - 1] - 5, (int)device_y[point_count - 1] - 5,
                    (int)device_x[point_count - 1] + 5, (int)device_y[point_count - 1] + 5);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        SelectObject(dc, old_brush);
        DeleteObject(mark);
    }
}

/*============================ Legend and scale ===============================*/

static void legend_line(HDC dc, int x, int y, COLORREF colour, int thickness, const char* text)
{
    HPEN pen = CreatePen(PS_SOLID, thickness, colour);
    HPEN old_pen = (HPEN)SelectObject(dc, pen);

    MoveToEx(dc, x, y + LINE_HEIGHT / 2, NULL);
    LineTo(dc, x + 26, y + LINE_HEIGHT / 2);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    SetTextColor(dc, RGB(45, 55, 70));
    TextOutA(dc, x + 34, y + 1, text, (int)strlen(text));
}

static void draw_legend(HDC dc)
{
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    HPEN   border = CreatePen(PS_SOLID, 1, RGB(190, 198, 210));
    HPEN   old_pen;
    HBRUSH old_brush;
    char   text[200];
    int    x = 14;
    int    y = 14;
    int    line;

    old_brush = (HBRUSH)SelectObject(dc, background);
    old_pen   = (HPEN)SelectObject(dc, border);
    Rectangle(dc, x, y, x + 372, y + 6 * LINE_HEIGHT + 20);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(background);
    DeleteObject(border);

    SetBkMode(dc, TRANSPARENT);
    line = y + 8;

    SetTextColor(dc, RGB(20, 30, 45));
    snprintf(text, sizeof(text), "Покрытие: %.150s", source_name);
    TextOutA(dc, x + 12, line + 1, text, (int)strlen(text));
    line += LINE_HEIGHT;

    legend_line(dc, x + 12, line, RGB(40, 140, 90), 3, "трек с покрытием");
    line += LINE_HEIGHT;

    snprintf(text, sizeof(text), "без покрытия, сырое: %.3f км", raw_uncovered);
    legend_line(dc, x + 12, line, RGB(214, 65, 55), 9, text);
    line += LINE_HEIGHT;

    snprintf(text, sizeof(text), "без покрытия, сглаженное: %.3f км", avg_uncovered);
    legend_line(dc, x + 12, line, RGB(250, 166, 26), 3, text);
    line += LINE_HEIGHT;

    SetTextColor(dc, RGB(90, 100, 115));
    snprintf(text, sizeof(text), "маршрут %.3f км, окно сглаживания %.2f км",
             track_length, smooth_window);
    TextOutA(dc, x + 12, line + 1, text, (int)strlen(text));
    line += LINE_HEIGHT;

    TextOutA(dc, x + 12, line + 1, "оранжевая идёт рядом с треком; красный без неё - убрано сглаживанием",
             (int)strlen("оранжевая идёт рядом с треком; красный без неё - убрано сглаживанием"));
}

static double nice_distance(double metres)
{
    double magnitude;
    double rest;

    if(metres < 1.0) return 1.0;
    magnitude = pow(10.0, floor(log10(metres)));
    rest = metres / magnitude;
    if(rest >= 5.0) return 5.0 * magnitude;
    if(rest >= 2.0) return 2.0 * magnitude;
    return magnitude;
}

static void draw_scale_bar(HDC dc, int width, int height)
{
    double metres_in_pixel = METRES_IN_DEGREE / view_scale;
    double distance = nice_distance(metres_in_pixel * 170.0);
    int    length = (int)(distance / metres_in_pixel + 0.5);
    int    x = 18;
    int    y = height - 22;
    HPEN   pen;
    HPEN   old_pen;
    char   text[64];

    (void)width;
    if(length < 10) length = 10;

    pen = CreatePen(PS_SOLID, 2, RGB(45, 55, 70));
    old_pen = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, x, y, NULL);
    LineTo(dc, x + length, y);
    MoveToEx(dc, x, y - 6, NULL);
    LineTo(dc, x, y + 5);
    MoveToEx(dc, x + length, y - 6, NULL);
    LineTo(dc, x + length, y + 5);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    if(distance >= 1000.0) snprintf(text, sizeof(text), "%.0f км", distance / 1000.0);
    else                   snprintf(text, sizeof(text), "%.0f м", distance);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(45, 55, 70));
    TextOutA(dc, x + length + 8, y - 9, text, (int)strlen(text));
}

static void draw_status(HDC dc, int width, int height)
{
    RECT strip;
    char text[300];

    strip.left = 0;
    strip.top = height;
    strip.right = width;
    strip.bottom = height + STATUS_HEIGHT;
    FillRect(dc, &strip, (HBRUSH)(COLOR_BTNFACE + 1));

    if(cursor_known && (point_count > 1))
    {
        double latitude, longitude;
        device_to_geo(cursor_position.x, cursor_position.y, width, height, &latitude, &longitude);
        snprintf(text, sizeof(text),
                 "  %.6f, %.6f   |   в пикселе %.1f м   |   колесо - масштаб, ЛКМ - сдвиг, "
                 "двойной клик - вписать трек",
                 latitude, longitude, METRES_IN_DEGREE / view_scale);
    }
    else
    {
        snprintf(text, sizeof(text),
                 "  колесо - масштаб, ЛКМ - сдвиг, двойной клик - вписать трек");
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(45, 55, 70));
    TextOutA(dc, 6, height + 5, text, (int)strlen(text));
}

/*============================== Cached bitmap ================================*/

static void drop_cache(void)
{
    if(cache_bitmap != NULL)
    {
        SelectObject(cache_dc, cache_old_bitmap);
        DeleteObject(cache_bitmap);
        cache_bitmap = NULL;
    }
    if(cache_dc != NULL)
    {
        DeleteDC(cache_dc);
        cache_dc = NULL;
    }
    cache_width = 0;
    cache_height = 0;
    cache_valid = 0;
}

static void ensure_cache(HDC window_dc, int width, int height)
{
    if(cache_dc == NULL) cache_dc = CreateCompatibleDC(window_dc);
    if(cache_dc == NULL) return;

    if((cache_bitmap == NULL) || (cache_width != width) || (cache_height != height))
    {
        if(cache_bitmap != NULL)
        {
            SelectObject(cache_dc, cache_old_bitmap);
            DeleteObject(cache_bitmap);
        }
        cache_bitmap = CreateCompatibleBitmap(window_dc, width, height);
        if(cache_bitmap == NULL) return;
        cache_old_bitmap = (HBITMAP)SelectObject(cache_dc, cache_bitmap);
        cache_width  = width;
        cache_height = height;
        cache_valid  = 0;
    }

    if(!cache_valid)
    {
        HFONT old_font = (HFONT)SelectObject(cache_dc, map_font);
        draw_map(cache_dc, width, height);
        SelectObject(cache_dc, old_font);
        cache_valid = 1;
    }
}

/*============================== Window procedure =============================*/

static void invalidate_status(void)
{
    RECT client;
    RECT strip;

    GetClientRect(map_window, &client);
    strip = client;
    strip.top = client.bottom - STATUS_HEIGHT;
    InvalidateRect(map_window, &strip, FALSE);
}

static LRESULT CALLBACK map_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch(message)
    {
    case WM_ERASEBKGND:
        return 1;                               /* everything is painted in WM_PAINT */

    case WM_SIZE:
        cache_valid = 0;
        InvalidateRect(window, NULL, FALSE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC         dc = BeginPaint(window, &paint);
        HFONT       old_font = (HFONT)SelectObject(dc, map_font);
        int         width, height;

        map_area(&width, &height);
        ensure_cache(dc, width, height);
        if(cache_bitmap != NULL) BitBlt(dc, 0, 0, width, height, cache_dc, 0, 0, SRCCOPY);
        if(point_count > 1)
        {
            draw_legend(dc);
            draw_scale_bar(dc, width, height);
        }
        draw_status(dc, width, height);

        SelectObject(dc, old_font);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        POINT position;
        int   steps = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
        double factor = pow(1.25, steps);

        position.x = (short)LOWORD(lparam);
        position.y = (short)HIWORD(lparam);
        ScreenToClient(window, &position);
        zoom_at(position.x, position.y, factor);
        return 0;
    }

    case WM_LBUTTONDOWN:
        drag_active = 1;
        drag_start.x = (short)LOWORD(lparam);
        drag_start.y = (short)HIWORD(lparam);
        drag_latitude  = view_latitude;
        drag_longitude = view_longitude;
        SetCapture(window);
        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        return 0;

    case WM_MOUSEMOVE:
        cursor_position.x = (short)LOWORD(lparam);
        cursor_position.y = (short)HIWORD(lparam);
        cursor_known = 1;
        if(drag_active)
        {
            int    width, height;
            double latitude, longitude;

            map_area(&width, &height);
            latitude  = drag_latitude + (cursor_position.y - drag_start.y) / view_scale;
            longitude = drag_longitude - (cursor_position.x - drag_start.x)
                                         / (view_squeeze * view_scale);
            set_centre(latitude, longitude);
            cache_valid = 0;
            InvalidateRect(window, NULL, FALSE);
        }
        else invalidate_status();
        return 0;

    case WM_LBUTTONUP:
        if(drag_active)
        {
            drag_active = 0;
            ReleaseCapture();
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        fit_track();
        InvalidateRect(window, NULL, FALSE);
        return 0;

    case WM_KEYDOWN:
        switch(wparam)
        {
        case VK_ESCAPE: DestroyWindow(window); return 0;
        case VK_HOME:   fit_track(); InvalidateRect(window, NULL, FALSE); return 0;
        case VK_ADD:
        case VK_OEM_PLUS:
        {
            int width, height;
            map_area(&width, &height);
            zoom_at(width / 2, height / 2, 1.25);
            return 0;
        }
        case VK_SUBTRACT:
        case VK_OEM_MINUS:
        {
            int width, height;
            map_area(&width, &height);
            zoom_at(width / 2, height / 2, 0.8);
            return 0;
        }
        default: break;
        }
        break;

    case WM_DESTROY:
        drop_cache();
        release_points();
        map_window = NULL;
        return 0;

    default: break;
    }

    return DefWindowProcA(window, message, wparam, lparam);
}

/*================================ Interface ==================================*/

static void create_map_font(void)
{
    NONCLIENTMETRICSA metrics;

    if(map_font != NULL) return;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if(SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        map_font = CreateFontIndirectA(&metrics.lfMessageFont);
    if(map_font == NULL) map_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static int register_map_class(void)
{
    static int registered = 0;
    WNDCLASSEXA window_class;

    if(registered) return 1;

    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize        = sizeof(window_class);
    window_class.style         = CS_DBLCLKS;        /* the double click resets the view */
    window_class.lpfnWndProc   = map_proc;
    window_class.hInstance     = map_instance;
    window_class.hCursor       = LoadCursor(NULL, IDC_ARROW);
    window_class.hIcon         = LoadIconA(map_instance, MAKEINTRESOURCEA(IDI_APPICON));
    window_class.hbrBackground = NULL;
    window_class.lpszClassName = MAP_CLASS_NAME;

    registered = RegisterClassExA(&window_class) ? 1 : 0;
    return registered;
}

void map_view_open(HINSTANCE instance_handle, HWND owner)
{
    char title[400];

    map_instance = instance_handle;
    create_map_font();
    if(!register_map_class())
    {
        printf("Карта: класс окна не зарегистрирован, код %lu\n", (unsigned long)GetLastError());
        return;
    }

    if(!collect_points())
    {
        MessageBoxA(owner,
                    "Карта строится по результату общего расчёта (TOT).\n"
                    "Выполните расчёт покрытия и откройте карту снова.",
                    "Карта покрытия", MB_ICONINFORMATION);
        return;
    }

    snprintf(title, sizeof(title), "Карта покрытия - %s", source_name);

    if(map_window == NULL)
    {
        map_window = CreateWindowExA(0, MAP_CLASS_NAME, title, WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760,
                                     owner, NULL, map_instance, NULL);
        if(map_window == NULL)
        {
            printf("Карта: окно не создано, код %lu\n", (unsigned long)GetLastError());
            return;
        }
        fit_track();
        ShowWindow(map_window, SW_SHOWNORMAL);
    }
    else
    {
        SetWindowTextA(map_window, title);
        fit_track();
        if(IsIconic(map_window)) ShowWindow(map_window, SW_RESTORE);
        SetForegroundWindow(map_window);
    }

    cache_valid = 0;
    InvalidateRect(map_window, NULL, FALSE);
}

void map_view_refresh(void)
{
    char title[400];

    if(map_window == NULL) return;
    if(!collect_points()) return;

    snprintf(title, sizeof(title), "Карта покрытия - %s", source_name);
    SetWindowTextA(map_window, title);
    cache_valid = 0;
    InvalidateRect(map_window, NULL, FALSE);
}

HWND map_view_hwnd(void)
{
    return map_window;
}
