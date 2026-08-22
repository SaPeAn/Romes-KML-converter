/*
 *  html_report - see html_report.h.
 *
 *  This file is windows-1251, and so is the page it writes: the folder names come from
 *  the kml in that encoding, and declaring it in <meta> keeps them readable without any
 *  conversion.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <windows.h>

#include "app_core.h"
#include "html_report.h"
#include "../kml_struct.h"

#define CHART_WIDTH    1120
#define CHART_HEIGHT   270
#define PLOT_LEFT      64
#define PLOT_TOP       14
#define PLOT_WIDTH     1036
#define PLOT_HEIGHT    212

/*----The page is written in utf-8 while the program works in windows-1251: the report
     should open anywhere, including a phone or a colleague's machine.  Everything that
     can carry Russian text goes through here.----*/
static void wr(FILE* file, const char* format, ...)
{
    char    ansi[8192];
    wchar_t wide[8192];
    char    utf8[16384];
    va_list args;

    va_start(args, format);
    vsnprintf(ansi, sizeof(ansi), format, args);
    va_end(args);

    if(MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, 8192) <= 0) { fputs(ansi, file); return; }
    if(WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), NULL, NULL) <= 0) { fputs(ansi, file); return; }
    fputs(utf8, file);
}

/*----The folder names may carry characters that mean something in html.----*/
static void write_escaped(FILE* file, const char* text)
{
    char   buffer[2048];
    size_t out = 0;

    if(text == NULL) return;
    while((*text != 0) && (out < (sizeof(buffer) - 8)))
    {
        switch(*text)
        {
        case '&': memcpy(buffer + out, "&amp;",  5); out += 5; break;
        case '<': memcpy(buffer + out, "&lt;",   4); out += 4; break;
        case '>': memcpy(buffer + out, "&gt;",   4); out += 4; break;
        case '"': memcpy(buffer + out, "&quot;", 6); out += 6; break;
        default:  buffer[out] = *text;               out += 1; break;
        }
        text++;
    }
    buffer[out] = 0;
    wr(file, "%s", buffer);
}

/*----The measurements themselves go into the page, so the chart can be redrawn at any
     zoom level: folding them into one bucket per pixel here, as a static picture does,
     would leave nothing to zoom into.  Levels are written as tenths of a dBm in whole
     numbers - that is short enough to keep the page reasonable.----*/
#define NO_LEVEL   (-32768)

static void write_series(FILE* file, const folder_t* folder, unsigned rows, int averaged)
{
    unsigned i;

    fputc('[', file);
    for(i = 0; i < rows; i++)
    {
        const placemark_t* point = &folder->placemark_arr[i];
        double             value = averaged ? point->averagelevel : point->signallevel;

        if(i > 0) fputc(',', file);
        if(point->hasdata) fprintf(file, "%d", (int)(value * 10.0 + (value < 0 ? -0.5 : 0.5)));
        else               fprintf(file, "%d", NO_LEVEL);
    }
    fputc(']', file);
}

/*----One chart: a canvas plus the data behind it.  The drawing and the mouse handling
     live in the shared script at the end of the page.----*/
/*----The distance along the track, in whole metres, shared by every chart on the page:
     the route is one and the same, so one array is enough.  It is computed here from the
     track coordinates - the csv keeps no distance column, and there is no need to add one.----*/
static void write_distance_array(FILE* file, const folder_t* track, unsigned rows)
{
    double   travelled = 0.0;
    unsigned i;

    fputs("<script>var dist=[0", file);
    for(i = 1; i < rows; i++)
    {
        travelled += distearth(track->placemark_arr[i].latitude,   track->placemark_arr[i].longitude,
                               track->placemark_arr[i-1].latitude, track->placemark_arr[i-1].longitude);
        fprintf(file, ",%d", (int)(travelled * 1000.0 + 0.5));
    }
    fputs("];</script>\n", file);
}

/*----One chart: a canvas plus the levels behind it.  The drawing and the mouse handling
     live in the shared script in the head of the page.----*/
static void write_chart(FILE* file, const folder_t* folder, unsigned rows, int index)
{
    if(rows == 0) return;

    wr(file, "<div class=\"chartbox\">\n");
    wr(file, "<canvas id=\"chart%d\" class=\"chart\"></canvas>\n", index);
    wr(file, "<div class=\"chartbar\"><span id=\"range%d\" class=\"range\"></span>"
             "<button type=\"button\" onclick=\"resetChart(%d)\">Весь маршрут</button>"
             "<span class=\"hint\">колесо мыши - масштаб по оси X, перетаскивание левой кнопкой - сдвиг</span></div>\n",
       index, index);
    wr(file, "</div>\n");

    fprintf(file, "<script>addChart({id:'chart%d',range:'range%d',thr:%.1f,", index, index, folder->threshold);

    fputs("lv:", file);
    write_series(file, folder, rows, 0);
    fputs(",av:", file);
    write_series(file, folder, rows, 1);
    fputs("});</script>\n", file);
}
static void write_style(FILE* file)
{
    fputs("<style>\n"
          "body{font:14px/1.5 'Segoe UI',Arial,sans-serif;margin:0;padding:28px 32px;color:#1d2129;background:#f4f5f7;}\n"
          "h1{font-size:22px;margin:0 0 4px;}\n"
          "h2{font-size:17px;margin:28px 0 10px;}\n"
          "h4{font-size:14px;margin:14px 0 6px;}\n"
          ".sub{color:#5c6470;margin-bottom:22px;}\n"
          ".card{background:#fff;border:1px solid #dfe2e7;border-radius:8px;padding:16px 18px;margin-bottom:18px;}\n"
          ".card h3{font-size:15px;margin:0 0 2px;word-break:break-all;}\n"
          ".meta{color:#5c6470;font-size:13px;margin-bottom:10px;}\n"
          "table{border-collapse:collapse;font-size:13px;}\n"
          "th,td{border:1px solid #dfe2e7;padding:5px 10px;text-align:right;}\n"
          "th{background:#eef1f5;font-weight:600;text-align:center;}\n"
          "td.name{text-align:left;}\n"
          ".params td.name{white-space:nowrap;}\n"
          ".chartbox{margin:8px 0 2px;}\n"
          ".chart{display:block;width:100%;height:280px;border:1px solid #dfe2e7;border-radius:4px;\n"
          "       background:#fbfcfd;cursor:grab;touch-action:none;}\n"
          ".chart.drag{cursor:grabbing;}\n"
          ".chartbar{display:flex;align-items:center;gap:14px;flex-wrap:wrap;margin:8px 0 2px;font-size:12px;color:#5c6470;}\n"
          ".chartbar button{font:inherit;padding:3px 10px;border:1px solid #c9ced6;border-radius:4px;\n"
          "                 background:#fff;color:#1d2129;cursor:pointer;}\n"
          ".chartbar button:hover{background:#eef1f5;}\n"
          ".range{font-weight:600;color:#1d2129;}\n"
          ".hint{color:#8a929c;}\n"
          ".legend{font-size:12px;color:#5c6470;margin-top:6px;}\n"
          ".legend i{display:inline-block;width:14px;height:8px;margin:0 5px 0 14px;vertical-align:middle;}\n"
          ".legend i:first-child{margin-left:0;}\n"
          ".sw-band{background:#9cc4e8;}\n"
          ".sw-avg{background:#1a5fa8;}\n"
          ".sw-thr{background:#e8453c;}\n"
          ".sw-gap{background:#f6cfcd;}\n"
          "</style>\n", file);
}

static void write_parameters(FILE* file, const init_t* settings)
{
    wr(file, "<h2>Параметры расчёта</h2>\n<div class=\"card\"><table class=\"params\">\n");
    wr(file, "<tr><td class=\"name\">Порог GSM</td><td>%.1f dBm</td>"
                  "<td class=\"name\">Тип расчёта</td><td>%s</td></tr>\n",
            settings->GSMcoveragelvl, (settings->covercalctype == total) ? "общий (TOT)" : "по элементам (SEP)");
    wr(file, "<tr><td class=\"name\">Порог UMTS</td><td>%.1f dBm</td>"
                  "<td class=\"name\">Усреднение уровня</td><td>%s</td></tr>\n",
            settings->UMTScoveragelvl, (settings->avgtype == median) ? "медиана (MED)" : "скользящее среднее (SMA)");
    wr(file, "<tr><td class=\"name\">Порог LTE</td><td>%.1f dBm</td>"
                  "<td class=\"name\">Точек усреднения уровня</td><td>%d</td></tr>\n",
            settings->LTEcoveragelvl, settings->avgdepth);
    wr(file, "<tr><td class=\"name\">Порог по умолчанию</td><td>%.1f dBm</td>"
                  "<td class=\"name\">Окно сглаживания</td><td>%.2f км</td></tr>\n",
            settings->defaultcovlvl, settings->maxskipdist);
    wr(file, "<tr><td class=\"name\">Автозаполнение пустых точек</td><td>%s</td>"
                  "<td class=\"name\"></td><td></td></tr>\n",
            settings->fillinflag ? "включено" : "выключено");
    wr(file, "</table></div>\n");
}

/*----Sections without coverage, as they go into Coverage_*.csv.----*/
static void write_gap_table(FILE* file, const folder_t* folder)
{
    unsigned j;
    unsigned shown = 0;

    if((folder->covreg == NULL) || (folder->reg_quantity == 0)) return;

    wr(file, "<table>\n<tr><th>участок</th><th>длина, км</th><th>начало, с.ш.</th><th>начало, в.д.</th>"
             "<th>конец, с.ш.</th><th>конец, в.д.</th></tr>\n");
    for(j = 0; j < folder->reg_quantity; j++)
    {
        if(folder->covreg[j].coverfl) continue;
        if(shown >= 500) break;                 /* a very fragmented track would blow up the page */
        shown++;
        wr(file, "<tr><td>%u</td><td>%.6f</td><td>%.6f</td><td>%.6f</td><td>%.6f</td><td>%.6f</td></tr>\n",
                shown, folder->covreg[j].distance, folder->covreg[j].startlat, folder->covreg[j].startlong,
                folder->covreg[j].endlat, folder->covreg[j].endlong);
    }
    wr(file, "</table>\n");
    if(shown == 0) wr(file, "<div class=\"meta\">Участков без покрытия нет.</div>\n");
}

/*----The drawing itself: one bucket per pixel of the current view, recomputed on every
     zoom or pan, so zooming in actually reveals the individual measurements.----*/
static void write_script(FILE* file)
{
    fputs("<script>\n"
"var charts = [];\n"
"var PAD_L = 58, PAD_R = 46, PAD_T = 12, PAD_B = 30, NO_LEVEL = -32768;\n"
"\n"
"function addChart(c){\n"
"  c.canvas = document.getElementById(c.id);\n"
"  c.label  = document.getElementById(c.range);\n"
"  c.n = c.lv.length;\n"
"  c.i0 = 0; c.i1 = c.n;\n"
"  var lo = c.thr, hi = c.thr, i, v;\n"
"  for(i = 0; i < c.n; i++){\n"
"    v = c.lv[i]; if(v === NO_LEVEL) continue;\n"
"    v = v / 10;\n"
"    if(v < lo) lo = v;\n"
"    if(v > hi) hi = v;\n"
"  }\n"
"  c.lo = Math.max(lo - 3, -140); c.hi = hi + 3;\n"
"  charts.push(c);\n"
"  attach(c);\n"
"  resizeChart(c);\n"
"}\n"
"\n"
"function distAt(i){ return (typeof dist !== 'undefined' && dist[i] !== undefined) ? dist[i] / 1000 : i; }\n"
"\n"
"function resizeChart(c){\n"
"  var box = c.canvas.getBoundingClientRect();\n"
"  c.dpr = window.devicePixelRatio || 1;\n"
"  c.canvas.width  = Math.max(1, Math.round(box.width  * c.dpr));\n"
"  c.canvas.height = Math.max(1, Math.round(box.height * c.dpr));\n"
"  drawChart(c);\n"
"}\n"
"\n"
"function setView(c, i0, span){\n"
"  if(span > c.n) span = c.n;\n"
"  if(span < 20)  span = Math.min(20, c.n);\n"
"  i0 = Math.round(i0);\n"
"  if(i0 < 0) i0 = 0;\n"
"  if(i0 + span > c.n) i0 = c.n - span;\n"
"  c.i0 = i0; c.i1 = i0 + span;\n"
"  drawChart(c);\n"
"}\n"
"\n"
"function resetChart(index){\n"
"  var c = charts.filter(function(x){ return x.id === 'chart' + index; })[0];\n"
"  if(c) setView(c, 0, c.n);\n"
"}\n"
"\n"
"function drawChart(c){\n"
"  var ctx = c.canvas.getContext('2d');\n"
"  var d = c.dpr, W = c.canvas.width, H = c.canvas.height;\n"
"  var L = PAD_L * d, R = PAD_R * d, T = PAD_T * d, B = PAD_B * d;\n"
"  var pw = Math.max(1, Math.round(W - L - R)), ph = Math.max(1, Math.round(H - T - B));\n"
"  var span = c.i1 - c.i0;\n"
"  var top = new Float32Array(pw), bot = new Float32Array(pw), avg = new Float32Array(pw);\n"
"  var ok = new Uint8Array(pw);\n"
"  var px, i, from, to, v, mn, mx, sum, cnt;\n"
"\n"
"  for(px = 0; px < pw; px++){\n"
"    from = c.i0 + Math.floor(px * span / pw);\n"
"    to   = c.i0 + Math.floor((px + 1) * span / pw);\n"
"    if(to <= from) to = from + 1;\n"
"    if(to > c.n) to = c.n;\n"
"    mn = 1e9; mx = -1e9; sum = 0; cnt = 0;\n"
"    for(i = from; i < to; i++){\n"
"      v = c.lv[i];\n"
"      if(v === NO_LEVEL) continue;\n"
"      v = v / 10;\n"
"      if(v < mn) mn = v;\n"
"      if(v > mx) mx = v;\n"
"      sum += c.av[i] / 10; cnt++;\n"
"    }\n"
"    ok[px] = cnt ? 1 : 0;\n"
"    if(cnt){ top[px] = mx; bot[px] = mn; avg[px] = sum / cnt; }\n"
"  }\n"
"\n"
"  function y(level){ return T + (c.hi - level) / (c.hi - c.lo) * ph; }\n"
"\n"
"  ctx.setTransform(1, 0, 0, 1, 0, 0);\n"
"  ctx.clearRect(0, 0, W, H);\n"
"  ctx.fillStyle = '#fbfcfd';\n"
"  ctx.fillRect(L, T, pw, ph);\n"
"\n"
"  ctx.fillStyle = '#f6cfcd';\n"
"  for(px = 0; px < pw; px++){\n"
"    if(!ok[px] || (avg[px] >= c.thr)) continue;\n"
"    var run = 1;\n"
"    while(((px + run) < pw) && ok[px + run] && (avg[px + run] < c.thr)) run++;\n"
"    ctx.fillRect(L + px, T, run, ph);\n"
"    px += run - 1;\n"
"  }\n"
"\n"
"  ctx.strokeStyle = '#e6e9ee'; ctx.lineWidth = 1; ctx.fillStyle = '#5c6470';\n"
"  ctx.font = (11 * d) + 'px Segoe UI, Arial'; ctx.textAlign = 'right'; ctx.textBaseline = 'middle';\n"
"  var step = 10, line;\n"
"  for(line = Math.ceil(c.lo / step) * step; line <= c.hi; line += step){\n"
"    var yy = Math.round(y(line)) + 0.5;\n"
"    ctx.beginPath(); ctx.moveTo(L, yy); ctx.lineTo(L + pw, yy); ctx.stroke();\n"
"    ctx.fillText(line.toFixed(0), L - 6 * d, yy);\n"
"  }\n"
"\n"
"  ctx.textAlign = 'center'; ctx.textBaseline = 'top';\n"
"  var kmSpan = distAt(Math.min(c.n, c.i1) - 1) - distAt(c.i0);\n"
"  var digits = (kmSpan < 2) ? 3 : ((kmSpan < 20) ? 2 : 1);\n"
"  var ticks = 8, t;\n"
"  for(t = 0; t <= ticks; t++){\n"
"    var xx = Math.round(L + pw * t / ticks) + 0.5;\n"
"    var idx = Math.min(c.n - 1, c.i0 + Math.round(span * t / ticks));\n"
"    ctx.beginPath(); ctx.moveTo(xx, T); ctx.lineTo(xx, T + ph); ctx.stroke();\n"
"    ctx.fillText(distAt(idx).toFixed(digits), xx, T + ph + 6 * d);\n"
"  }\n"
"\n"
"  ctx.fillStyle = 'rgba(84,152,224,0.55)';\n"
"  ctx.strokeStyle = 'rgba(64,132,206,0.85)';\n"
"  ctx.lineWidth = 1 * d;\n"
"  ctx.beginPath();\n"
"  var started = false;\n"
"  for(px = 0; px < pw; px++){ if(!ok[px]) continue;\n"
"    if(!started){ ctx.moveTo(L + px, y(top[px])); started = true; }\n"
"    else ctx.lineTo(L + px, y(top[px])); }\n"
"  for(px = pw - 1; px >= 0; px--){ if(ok[px]) ctx.lineTo(L + px, y(bot[px])); }\n"
"  if(started){ ctx.closePath(); ctx.fill(); ctx.stroke(); }\n"
"\n"
"  ctx.strokeStyle = '#1a5fa8'; ctx.lineWidth = 1.6 * d;\n"
"  ctx.beginPath(); started = false;\n"
"  for(px = 0; px < pw; px++){ if(!ok[px]) continue;\n"
"    if(!started){ ctx.moveTo(L + px, y(avg[px])); started = true; }\n"
"    else ctx.lineTo(L + px, y(avg[px])); }\n"
"  ctx.stroke();\n"
"\n"
"  ctx.strokeStyle = '#e8453c'; ctx.lineWidth = 1.4 * d; ctx.setLineDash([6 * d, 4 * d]);\n"
"  var yt = Math.round(y(c.thr)) + 0.5;\n"
"  ctx.beginPath(); ctx.moveTo(L, yt); ctx.lineTo(L + pw, yt); ctx.stroke();\n"
"  ctx.setLineDash([]);\n"
"  ctx.fillStyle = '#e8453c'; ctx.textAlign = 'left'; ctx.textBaseline = 'middle';\n"
"  ctx.fillText(c.thr.toFixed(1), L + pw + 5 * d, yt);\n"
"\n"
"  ctx.fillStyle = '#5c6470'; ctx.textAlign = 'left'; ctx.textBaseline = 'top';\n"
"  ctx.fillText('dBm', 6 * d, T);\n"
"  ctx.textAlign = 'center';\n"
"  ctx.fillText('\u0440\u0430\u0441\u0441\u0442\u043e\u044f\u043d\u0438\u0435 \u043e\u0442 \u043d\u0430\u0447\u0430\u043b\u0430, \u043a\u043c', L + pw / 2, H - 14 * d);\n"
"\n"
"  ctx.strokeStyle = '#dfe2e7'; ctx.lineWidth = 1;\n"
"  ctx.strokeRect(L + 0.5, T + 0.5, pw, ph);\n"
"\n"
"  if(c.label) c.label.textContent = distAt(c.i0).toFixed(2) + ' - ' + distAt(Math.min(c.n, c.i1) - 1).toFixed(2) +\n"
"                                    ' \u043a\u043c (' + span + ' \u0442\u043e\u0447\u0435\u043a)';\n"
"}\n"
"\n"
"function attach(c){\n"
"  var cv = c.canvas;\n"
"\n"
"  cv.addEventListener('wheel', function(e){\n"
"    e.preventDefault();\n"
"    var box = cv.getBoundingClientRect();\n"
"    var pw = Math.max(1, box.width - PAD_L - PAD_R);\n"
"    var x = e.clientX - box.left - PAD_L;\n"
"    if(x < 0) x = 0; if(x > pw) x = pw;\n"
"    var span = c.i1 - c.i0;\n"
"    var anchor = c.i0 + x / pw * span;\n"
"    var next = Math.round(span * ((e.deltaY < 0) ? 0.8 : 1.25));\n"
"    if(next < 20) next = Math.min(20, c.n);\n"
"    if(next > c.n) next = c.n;\n"
"    setView(c, anchor - (anchor - c.i0) * next / span, next);\n"
"  }, {passive: false});\n"
"\n"
"  cv.addEventListener('mousedown', function(e){\n"
"    c.dragX = e.clientX; c.dragI0 = c.i0;\n"
"    cv.classList.add('drag');\n"
"    e.preventDefault();\n"
"  });\n"
"\n"
"  window.addEventListener('mousemove', function(e){\n"
"    if(c.dragX === undefined || c.dragX === null) return;\n"
"    var box = cv.getBoundingClientRect();\n"
"    var pw = Math.max(1, box.width - PAD_L - PAD_R);\n"
"    var span = c.i1 - c.i0;\n"
"    setView(c, c.dragI0 - (e.clientX - c.dragX) * span / pw, span);\n"
"  });\n"
"\n"
"  window.addEventListener('mouseup', function(){\n"
"    if(c.dragX === undefined || c.dragX === null) return;\n"
"    c.dragX = null; cv.classList.remove('drag');\n"
"  });\n"
"\n"
"  cv.addEventListener('dblclick', function(){ setView(c, 0, c.n); });\n"
"}\n"
"\n"
"window.addEventListener('resize', function(){ charts.forEach(resizeChart); });\n"
"</script>\n", file);
}

int html_report_write(const char* path)
{
    const init_t*   settings;
    const folder_t* averaged;
    const folder_t* track;
    FILE*           file;
    time_t          now;
    char            stamp[64];
    unsigned        rows;
    int             folders, gps, i;

    if(!app_result_analysed()) return 0;

    settings = app_result_settings();
    folders  = app_result_folders();
    gps      = app_result_gps_folder();
    rows     = app_result_rows();
    if(gps < 0) gps = 0;
    track    = app_result_folder(gps);
    averaged = app_result_folder(folders);          /* the reserved slot */
    if((track == NULL) || (averaged == NULL)) return 0;

    file = fopen(path, "w");
    if(file == NULL) return 0;

    time(&now);
    strftime(stamp, sizeof(stamp), "%d.%m.%Y %H:%M", localtime(&now));

    wr(file, "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"utf-8\">\n");
    wr(file, "<title>Отчёт по покрытию - %s</title>\n", app_result_source_name());
    write_style(file);
    write_script(file);          /* before the charts: they call addChart() as they appear */
    wr(file, "</head>\n<body>\n");

    wr(file, "<h1>Отчёт по покрытию</h1>\n");
    wr(file, "<div class=\"sub\">Файл <b>%s</b> &nbsp;&middot;&nbsp; точек измерений: %u &nbsp;&middot;&nbsp; сформирован %s</div>\n",
            app_result_source_name(), rows, stamp);

    write_parameters(file, settings);

    wr(file, "<h2>Итоги</h2>\n<div class=\"card\">\n");
    if(settings->covercalctype == total)
    {
        wr(file, "<table>\n<tr><th>величина</th><th>км</th></tr>\n");
        wr(file, "<tr><td class=\"name\">Общая длина маршрута</td><td>%.3f</td></tr>\n", averaged->totdist);
        wr(file, "<tr><td class=\"name\">С покрытием</td><td>%.3f</td></tr>\n", averaged->covtotdist);
        wr(file, "<tr><td class=\"name\">Без покрытия</td><td>%.3f</td></tr>\n", averaged->uncovtotdist);
        wr(file, "<tr><td class=\"name\">Без покрытия, без сглаживания по расстоянию</td><td>%.3f</td></tr>\n",
                track->uncovtotdist);
        wr(file, "</table>\n");
        wr(file, "<div class=\"meta\">Общий расчёт: точка считается покрытой, если хотя бы один выбранный элемент даёт уровень выше своего порога.</div>\n");
    }
    else
    {
        wr(file, "<table>\n<tr><th>элемент</th><th>всего, км</th><th>с покрытием, км</th><th>без покрытия, км</th></tr>\n");
        for(i = 0; i < folders; i++)
        {
            const folder_t* folder = app_result_folder(i);
            if(!app_result_selected(i) || (folder == NULL)) continue;
            wr(file, "<tr><td class=\"name\">");
            write_escaped(file, folder->name);
            wr(file, "</td><td>%.3f</td><td>%.3f</td><td>%.3f</td></tr>\n",
                    folder->totdist, folder->covtotdist, folder->uncovtotdist);
        }
        wr(file, "</table>\n");
    }
    wr(file, "</div>\n");

    wr(file, "<h2>Уровни сигнала по элементам</h2>\n");
    write_distance_array(file, track, rows);   /* one distance array for all charts on the page */
    for(i = 0; i < folders; i++)
    {
        const folder_t* folder = app_result_folder(i);
        if(!app_result_selected(i) || (folder == NULL)) continue;

        wr(file, "<div class=\"card\">\n<h3>");
        write_escaped(file, folder->name);
        wr(file, "</h3>\n");
        wr(file, "<div class=\"meta\">Порог покрытия %.1f dBm", folder->threshold);
        if(settings->covercalctype == separate)
            wr(file, " &nbsp;&middot;&nbsp; без покрытия %.3f км из %.3f км", folder->uncovtotdist, folder->totdist);
        wr(file, "</div>\n");

        write_chart(file, folder, rows, i);

        wr(file, "<div class=\"legend\"><i class=\"sw-band\"></i>измеренный уровень (мин-макс)"
                 "<i class=\"sw-avg\"></i>усреднённый уровень"
                 "<i class=\"sw-thr\"></i>порог"
                 "<i class=\"sw-gap\"></i>участки ниже порога</div>\n");

        if(settings->covercalctype == separate)
        {
            wr(file, "<h4>Участки без покрытия</h4>\n");
            write_gap_table(file, folder);
        }
        wr(file, "</div>\n");
    }


    wr(file, "</body>\n</html>\n");
    fclose(file);
    return 1;
}
