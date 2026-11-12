#ifndef WININCLUDE_H
#define WININCLUDE_H

#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdbool.h>

static DWORD orig_out_mode = 0;
static DWORD orig_in_mode = 0;
static HWND hwnd = NULL;

// Инициализация консоли: включаем ANSI-цвета и прячем курсор
static inline void sys_init(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(hOut, &orig_out_mode);
    GetConsoleMode(hIn, &orig_in_mode);

    // Врубаем Virtual Terminal Processing для ANSI-графики
    DWORD out_mode = orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, out_mode);

    // Получаем хэндл окна терминала
    hwnd = GetConsoleWindow();

    // Прячем курсор
    printf("\033[?25l");
    fflush(stdout);
}

// Восстановление консоли при выходе
static inline void sys_cleanup(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    // Показываем курсор обратно и сбрасываем цвета
    printf("\033[?25h\033[0m\n");
    fflush(stdout);

    SetConsoleMode(hOut, orig_out_mode);
    SetConsoleMode(hIn, orig_in_mode);
}

// Получить абсолютные координаты окна на рабочем столе (ВИНДА ДАЕТ ЭТО БЕЗ ПРОБЛЕМ)
static inline void sys_get_window_pos(int *x, int *y) {
    if (hwnd) {
        RECT rect;
        GetWindowRect(hwnd, &rect);
        *x = rect.left;
        *y = rect.top;
    } else {
        *x = 0;
        *y = 0;
    }
}

// Размеры терминала в символах (колонок x строк)
static inline void sys_get_term_size(int *cols, int *rows) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

// Неблокирующая проверка нажатия клавиши
static inline int sys_kbhit(void) {
    return _kbhit();
}

// Считать символ без ожидания Enter
static inline int sys_getch(void) {
    return _getch();
}

// Сон в миллисекундах
static inline void sys_sleep_ms(int ms) {
    Sleep(ms);
}

#endif // WININCLUDE_H