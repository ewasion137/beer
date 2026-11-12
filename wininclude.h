#ifndef WININCLUDE_H
#define WININCLUDE_H

#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdbool.h>

static DWORD orig_out_mode = 0;
static UINT orig_cp = 0;
static HWND target_hwnd = NULL;

static inline void sys_init(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hOut, &orig_out_mode);

    // Включаем UTF-8 для красивых рамок бокала
    orig_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    // Включаем ANSI VT-последовательности
    DWORD out_mode = orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, out_mode);

    // Берем активное окно (Alacritty / Windows Terminal)
    target_hwnd = GetForegroundWindow();

    // Прячем курсор
    printf("\033[?25l");
    fflush(stdout);
}

static inline void sys_cleanup(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    printf("\033[?25h\033[0m\n");
    fflush(stdout);
    SetConsoleMode(hOut, orig_out_mode);
    SetConsoleOutputCP(orig_cp);
}

static inline void sys_get_window_pos(int *x, int *y) {
    if (!target_hwnd) target_hwnd = GetForegroundWindow();
    if (target_hwnd) {
        RECT rect;
        GetWindowRect(target_hwnd, &rect);
        *x = rect.left;
        *y = rect.top;
    } else {
        *x = 0; *y = 0;
    }
}

// Позиция курсора мыши на экране
static inline void sys_get_cursor_pos(int *x, int *y) {
    POINT pt;
    GetCursorPos(&pt);
    *x = pt.x;
    *y = pt.y;
}

// Нажата ли левая кнопка мыши
static inline bool sys_is_lbutton_down(void) {
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

static inline void sys_get_term_size(int *cols, int *rows) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

static inline int sys_kbhit(void) { return _kbhit(); }
static inline int sys_getch(void) { return _getch(); }
static inline void sys_sleep_ms(int ms) { Sleep(ms); }

#endif