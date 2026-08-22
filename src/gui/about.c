/*
 *  about - the "О программе" window: the manual, shown as it is written.
 *
 *  The text comes from instruction.c, the same one the console version writes into
 *  Инструкция.txt, so the two can not disagree.  Only the part about the window is
 *  shown here - the console section would only confuse.
 *
 *  The text is laid out with spaces, so the box uses a fixed pitch font; and an edit
 *  control breaks lines on CRLF only, so the newlines are doubled up on the way in.
 *
 *  This file is windows-1251, like the rest of the gui.
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "about.h"
#include "resource.h"
#include "../instruction.h"

#define ABOUT_CLASS_NAME "RomesCovAboutWindow"
#define IDC_ABOUT_TEXT   2001
#define IDC_ABOUT_CLOSE  2002

static HINSTANCE about_instance;
static HWND      about_window;
static HWND      about_text;
static HWND      about_close;
static HFONT     about_font;
static HFONT     about_button_font;

/*----The text box swallows the keys, so Esc is caught there and closes the window.----*/
static LRESULT CALLBACK text_subclass(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                      UINT_PTR id, DWORD_PTR reference)
{
    (void)id; (void)reference;
    if((message == WM_KEYDOWN) && (wparam == VK_ESCAPE))
    {
        DestroyWindow(GetParent(window));
        return 0;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

/*----Joins the parts of the manual and turns every newline into a pair, which is the
     only line break an edit control understands.  The caller frees the result.----*/
static char* build_text(void)
{
    const char* parts[3];
    size_t      length = 0;
    size_t      used = 0;
    char*       text;
    int         i;

    parts[0] = instruction_window;
    parts[1] = instruction_terms;
    parts[2] = instruction_contact;

    for(i = 0; i < 3; i++) length += strlen(parts[i]);
    text = (char*)malloc(length * 2 + 1);
    if(text == NULL) return NULL;

    for(i = 0; i < 3; i++)
    {
        const char* source = parts[i];
        while(*source != 0)
        {
            if(*source == '\n') text[used++] = '\r';
            text[used++] = *source++;
        }
    }
    text[used] = 0;
    return text;
}

static void create_fonts(void)
{
    NONCLIENTMETRICSA metrics;

    if(about_font == NULL)
    {
        about_font = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        if(about_font == NULL) about_font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    }

    if(about_button_font == NULL)
    {
        memset(&metrics, 0, sizeof(metrics));
        metrics.cbSize = sizeof(metrics);
        if(SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            about_button_font = CreateFontIndirectA(&metrics.lfMessageFont);
        if(about_button_font == NULL) about_button_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
}

static void layout(int width, int height)
{
    MoveWindow(about_text, 12, 12, width - 24, height - 58, TRUE);
    MoveWindow(about_close, width - 120, height - 38, 108, 26, TRUE);
}

static LRESULT CALLBACK about_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch(message)
    {
    case WM_CREATE:
    {
        char* text;

        about_text = CreateWindowExA(0, "EDIT", "",
                                     WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP |
                                     ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                     0, 0, 10, 10, window, (HMENU)(INT_PTR)IDC_ABOUT_TEXT,
                                     about_instance, NULL);
        about_close = CreateWindowExA(0, "BUTTON", "Закрыть",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                      0, 0, 10, 10, window, (HMENU)(INT_PTR)IDC_ABOUT_CLOSE,
                                      about_instance, NULL);
        SendMessage(about_text, WM_SETFONT, (WPARAM)about_font, TRUE);
        SendMessage(about_close, WM_SETFONT, (WPARAM)about_button_font, TRUE);

        text = build_text();
        if(text != NULL)
        {
            SetWindowTextA(about_text, text);
            free(text);
        }
        SendMessage(about_text, EM_SETSEL, (WPARAM)0, (LPARAM)0);
        SetWindowSubclass(about_text, text_subclass, 1, 0);
        return 0;
    }

    case WM_SIZE:
        layout(LOWORD(lparam), HIWORD(lparam));
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lparam)->ptMinTrackSize.x = 560;
        ((MINMAXINFO*)lparam)->ptMinTrackSize.y = 360;
        return 0;

    case WM_COMMAND:
        if(LOWORD(wparam) == IDC_ABOUT_CLOSE) { DestroyWindow(window); return 0; }
        break;

    case WM_CTLCOLORSTATIC:
        /* a read-only edit asks with this message; white keeps it looking like a page */
        SetBkMode((HDC)wparam, OPAQUE);
        SetBkColor((HDC)wparam, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);

    case WM_DESTROY:
        about_window = NULL;
        return 0;

    default: break;
    }

    return DefWindowProcA(window, message, wparam, lparam);
}

static int register_about_class(void)
{
    static int registered = 0;
    WNDCLASSEXA window_class;

    if(registered) return 1;

    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize        = sizeof(window_class);
    window_class.lpfnWndProc   = about_proc;
    window_class.hInstance     = about_instance;
    window_class.hCursor       = LoadCursor(NULL, IDC_ARROW);
    window_class.hIcon         = LoadIconA(about_instance, MAKEINTRESOURCEA(IDI_APPICON));
    window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    window_class.lpszClassName = ABOUT_CLASS_NAME;

    registered = RegisterClassExA(&window_class) ? 1 : 0;
    return registered;
}

void about_show(HINSTANCE instance_handle, HWND owner)
{
    int  width = 880;
    int  height = 700;
    int  x = CW_USEDEFAULT;
    int  y = CW_USEDEFAULT;
    RECT frame;

    about_instance = instance_handle;
    create_fonts();
    if(!register_about_class()) return;

    if(about_window != NULL)
    {
        if(IsIconic(about_window)) ShowWindow(about_window, SW_RESTORE);
        SetForegroundWindow(about_window);
        return;
    }

    if((owner != NULL) && GetWindowRect(owner, &frame))
    {
        x = frame.left + ((frame.right - frame.left) - width) / 2;
        y = frame.top + ((frame.bottom - frame.top) - height) / 2;
        if(x < 0) x = 0;
        if(y < 0) y = 0;
    }

    about_window = CreateWindowExA(0, ABOUT_CLASS_NAME, "О программе - RomesCov",
                                   WS_OVERLAPPEDWINDOW, x, y, width, height,
                                   owner, NULL, about_instance, NULL);
    if(about_window == NULL) return;

    ShowWindow(about_window, SW_SHOWNORMAL);
    UpdateWindow(about_window);
}

HWND about_hwnd(void)
{
    return about_window;
}
