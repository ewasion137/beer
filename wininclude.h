#ifndef WININCLUDE_H
#define WININCLUDE_H

#define WIN32_LEAN_AND_MEAN
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>


__declspec(dllimport) UINT WINAPI timeBeginPeriod(UINT uPeriod);
__declspec(dllimport) UINT WINAPI timeEndPeriod(UINT uPeriod);
#include <mmsystem.h>


#ifndef KEY_LEFT
#define KEY_LEFT 1001
#define KEY_RIGHT 1002
#define KEY_UP 1003
#define KEY_DOWN 1004
#endif

static DWORD orig_out_mode = 0;
static DWORD orig_in_mode = 0;
static UINT orig_cp = 0;
static HWND target_hwnd = NULL;

static bool drag_active = false;
static POINT drag_cur0;
static RECT drag_rect0;

static inline HWND get_alacritty_hwnd(void) {
    if (target_hwnd && IsWindow(target_hwnd))
        return target_hwnd;

    HWND fg = GetForegroundWindow();
    if (fg && IsWindow(fg)) {
        HWND root = GetAncestor(fg, GA_ROOT);
        target_hwnd = root ? root : fg;
        return target_hwnd;
    }
    target_hwnd = GetConsoleWindow();
    return target_hwnd;
}

static inline bool window_is_foreground(HWND h) {
    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    HWND root = GetAncestor(fg, GA_ROOT);
    return (root ? root : fg) == h;
}

static inline void sys_init(void) {
    SetProcessDPIAware();
    timeBeginPeriod(1);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(hOut, &orig_out_mode);
    GetConsoleMode(hIn, &orig_in_mode);

    orig_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    DWORD out_mode = orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, out_mode);

    DWORD in_mode = orig_in_mode;
    in_mode &=
        ~(ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT);
    in_mode |= ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(hIn, in_mode);

    FlushConsoleInputBuffer(hIn);
    get_alacritty_hwnd();

    printf("\033[?25l");
    fflush(stdout);
}

static inline void sys_cleanup(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    printf("\033[?25h\033[0m\n");
    fflush(stdout);

    SetConsoleMode(hOut, orig_out_mode);
    SetConsoleMode(hIn, orig_in_mode);
    SetConsoleOutputCP(orig_cp);
    timeEndPeriod(1);
}

static inline double sys_time_sec(void) {
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

static inline void sys_get_cursor_pos(int *x, int *y) {
    POINT pt;
    GetCursorPos(&pt);
    *x = pt.x;
    *y = pt.y;
}

static inline bool sys_window_drag_update(void) {
    bool down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    HWND h = get_alacritty_hwnd();
    if (!down || !h || !IsWindow(h)) {
        drag_active = false;
        return false;
    }

    POINT pt;
    GetCursorPos(&pt);

    if (!drag_active) {
        RECT r;
        GetWindowRect(h, &r);
        if (!PtInRect(&r, pt) || !window_is_foreground(h))
            return false;
        drag_active = true;
        drag_cur0 = pt;
        drag_rect0 = r;
    }

    SetWindowPos(h, NULL, drag_rect0.left + (pt.x - drag_cur0.x),
                 drag_rect0.top + (pt.y - drag_cur0.y), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_ASYNCWINDOWPOS);
    return true;
}

static inline bool sys_is_lbutton_down(void) {
    HWND h = get_alacritty_hwnd();
    if (!h || !window_is_foreground(h))
        return false;
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

static inline void sys_get_term_size(int *cols, int *rows) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

static inline int sys_kbhit(void) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD num_events = 0;
    if (!GetNumberOfConsoleInputEvents(hIn, &num_events) || num_events == 0)
        return 0;

    INPUT_RECORD records[32];
    DWORD read_count = 0;
    if (!PeekConsoleInput(hIn, records, 32, &read_count))
        return 0;

    for (DWORD i = 0; i < read_count; i++) {
        if (records[i].EventType == KEY_EVENT &&
            records[i].Event.KeyEvent.bKeyDown)
            return 1;
    }
    FlushConsoleInputBuffer(hIn);
    return 0;
}

static inline int sys_getch(void) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD ir;
    DWORD read_count;

    while (ReadConsoleInput(hIn, &ir, 1, &read_count) && read_count > 0) {
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
            if (vk == VK_LEFT)
                return KEY_LEFT;
            if (vk == VK_RIGHT)
                return KEY_RIGHT;
            if (vk == VK_DOWN)
                return KEY_DOWN;
            if (vk == VK_UP)
                return KEY_UP;
            if (vk == VK_ESCAPE)
                return 27;
            char c = ir.Event.KeyEvent.uChar.AsciiChar;
            if (c != 0)
                return (unsigned char)c;
        }
    }
    return 0;
}

static inline void sys_sleep_ms(int ms) { Sleep(ms); }

#endif
