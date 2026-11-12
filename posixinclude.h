#ifndef POSIXINCLUDE_H
#define POSIXINCLUDE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdbool.h>

static struct termios orig_termios;

// Инициализация: сырой режим ввода (raw mode) и прячем курсор
static inline void sys_init(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    
    // Отключаем эхо и канонический режим (ловим клавиши на лету)
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    // Прячем курсор
    printf("\033[?25l");
    fflush(stdout);
}

// Восстановление терминала
static inline void sys_cleanup(void) {
    printf("\033[?25h\033[0m\n");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Координаты окна (заглушка или xdotool, если нет линковки с Xlib)
static inline void sys_get_window_pos(int *x, int *y) {
    // В POSIX без костылей под конкретный WM сложно, пока заглушка 0,0
    // (потом замутим через пайп к xdotool или сокет hyprland)
    *x = 0;
    *y = 0;
}

// Размеры терминала через ioctl
static inline void sys_get_term_size(int *cols, int *rows) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    *cols = w.ws_col;
    *rows = w.ws_row;
}

// Неблокирующая проверка нажатия
static inline int sys_kbhit(void) {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

// Чтение символа
static inline int sys_getch(void) {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) < 0) return 0;
    return c;
}

// Сон
static inline void sys_sleep_ms(int ms) {
    usleep(ms * 1000);
}

#endif // POSIXINCLUDE_H