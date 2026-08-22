/*
 *  about - the "About" window with the manual in it.
 */

#ifndef __ABOUT_H__
#define __ABOUT_H__

#include <windows.h>

/*----Opens the window, or brings it to the front when it is already open.----*/
void about_show(HINSTANCE instance, HWND owner);

/*----The window, or NULL: the message loop keeps IsDialogMessage() away from it.----*/
HWND about_hwnd(void);

#endif
