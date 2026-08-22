/*
 *  map_view - a separate window that draws the coverage on a plain map.
 *
 *  It replaces the habit of opening _rawcov.kml and _avgcov.kml side by side: the track
 *  is drawn once, and the stretches without coverage are painted twice - as the raw
 *  calculation left them and as they look after the smoothing by distance.
 */

#ifndef __MAP_VIEW_H__
#define __MAP_VIEW_H__

#include <windows.h>

/*----Opens the map window, or brings it to the front when it is already open.  The data
     is copied out of app_core on the way in, so the map keeps its picture while the main
     window starts another job.----*/
void map_view_open(HINSTANCE instance, HWND owner);

/*----Takes a fresh copy of the results when the window is open; does nothing otherwise.
     Called after a successful analysis so the map does not show yesterday's coverage.----*/
void map_view_refresh(void);

/*----The map window, or NULL.  The message loop needs it to keep IsDialogMessage() of
     the main window away from keys that belong to the map.----*/
HWND map_view_hwnd(void);

#endif
