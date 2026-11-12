#ifndef POSIXINCLUDE_H
#define POSIXINCLUDE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdbool.h>

#ifndef KEY_LEFT
#define KEY_LEFT  1001
#define KEY_RIGHT 1002
#define KEY_UP    1003
#define KEY_DOWN  1004
#endif

static struct termios orig_termios;

static inline void sys_init(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    printf("\033[?25l");
    fflush(stdout);
}

static inline void sys_cleanup(void) {
    printf("\033[?25h\033[0m\n");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static inline void sys_get_term_size(int *cols, int *rows) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        *cols = w.ws_col;
        *rows = w.ws_row;
    }
}

static inline int sys_kbhit(void) {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

// Читает клавишу; стрелки (ESC [ A/B/C/D) превращает в KEY_*
static inline int sys_getch(void) {
    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) <= 0) return 0;
    if (c == 27) {
        if (!sys_kbhit()) return 27;
        unsigned char s[2] = {0, 0};
        if (read(STDIN_FILENO, &s[0], 1) <= 0) return 27;
        if (s[0] == '[' || s[0] == 'O') {
            if (read(STDIN_FILENO, &s[1], 1) <= 0) return 0;
            switch (s[1]) {
                case 'D': return KEY_LEFT;
                case 'C': return KEY_RIGHT;
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
            }
            return 0;
        }
        return 27;
    }
    return c;
}

static inline void sys_sleep_ms(int ms) {
    usleep((useconds_t)ms * 1000);
}

#endif // POSIXINCLUDE_H