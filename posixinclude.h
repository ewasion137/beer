#ifndef POSIXINCLUDE_H
#define POSIXINCLUDE_H

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>


#ifndef KEY_LEFT
#define KEY_LEFT 1001
#define KEY_RIGHT 1002
#define KEY_UP 1003
#define KEY_DOWN 1004
#endif

enum { WM_NONE = 0, WM_HYPR = 1, WM_SWAY = 2 };

static struct termios orig_termios;
static int wm_type = WM_NONE;
static char hypr_sock[512];
static char hypr_addr[64];
static int sway_fd = -1;
static long sway_con_id = -1;

static bool win_floating = false;
static int win_x = 0, win_y = 0, win_w = 0, win_h = 0;

static bool posix_lmb = false;
static bool posix_rmb = false;
static int posix_mx = 0, posix_my = 0;

static bool has_xtwinops = false;
static int xtwin_x = 0, xtwin_y = 0;

static bool drag_active = false;
static int drag_cur0_x = 0, drag_cur0_y = 0;
static int drag_win0_x = 0, drag_win0_y = 0;

static int key_queue[64];
static int key_head = 0, key_tail = 0;

static void key_push(int k) {
    int next = (key_head + 1) & 63;
    if (next != key_tail) {
        key_queue[key_head] = k;
        key_head = next;
    }
}

static int unix_connect(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct timeval tv = {0, 20000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    strcpy(addr.sun_path, path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    while (len > 0) {
        ssize_t n = send(fd, p, len, flags);
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int hypr_req(const char *cmd, char *out, size_t out_sz) {
    int fd = unix_connect(hypr_sock);
    if (fd < 0)
        return -1;
    if (send_all(fd, cmd, strlen(cmd)) < 0) {
        close(fd);
        return -1;
    }
    size_t tot = 0;
    while (tot + 1 < out_sz) {
        ssize_t n = recv(fd, out + tot, out_sz - 1 - tot, 0);
        if (n <= 0)
            break;
        tot += (size_t)n;
    }
    out[tot] = '\0';
    close(fd);
    return (int)tot;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

static bool json_str(const char *src, const char *key, char *out,
                     size_t out_sz) {
    const char *p = strstr(src, key);
    if (!p)
        return false;
    p = skip_ws(p + strlen(key));
    if (*p != '"')
        return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_sz)
        out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

static bool json_int(const char *src, const char *key, int *v) {
    const char *p = strstr(src, key);
    if (!p)
        return false;
    p = skip_ws(p + strlen(key));
    char *end = NULL;
    long val = strtol(p, &end, 10);
    if (end == p)
        return false;
    *v = (int)val;
    return true;
}

static bool json_pair(const char *src, const char *key, int *a, int *b) {
    const char *p = strstr(src, key);
    if (!p)
        return false;
    p = skip_ws(p + strlen(key));
    if (*p != '[')
        return false;
    p = skip_ws(p + 1);
    char *end = NULL;
    long v1 = strtol(p, &end, 10);
    if (end == p)
        return false;
    p = skip_ws(end);
    if (*p != ',')
        return false;
    p = skip_ws(p + 1);
    long v2 = strtol(p, &end, 10);
    if (end == p)
        return false;
    *a = (int)v1;
    *b = (int)v2;
    return true;
}

static bool json_bool(const char *src, const char *key, bool *v) {
    const char *p = strstr(src, key);
    if (!p)
        return false;
    p = skip_ws(p + strlen(key));
    if (strncmp(p, "true", 4) == 0) {
        *v = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *v = false;
        return true;
    }
    return false;
}

static int sway_req(uint32_t type, const char *payload, char *out,
                    size_t out_sz) {
    if (sway_fd < 0)
        return -1;
    size_t plen = payload ? strlen(payload) : 0;
    uint8_t hdr[14];
    memcpy(hdr, "i3-ipc", 6);
    uint32_t l32 = (uint32_t)plen;
    memcpy(hdr + 6, &l32, 4);
    memcpy(hdr + 10, &type, 4);

    if (send_all(sway_fd, hdr, 14) < 0 ||
        (plen > 0 && send_all(sway_fd, payload, plen) < 0)) {
        close(sway_fd);
        sway_fd = -1;
        return -1;
    }

    size_t got = 0;
    while (got < 14) {
        ssize_t n = recv(sway_fd, (char *)hdr + got, 14 - got, 0);
        if (n <= 0) {
            close(sway_fd);
            sway_fd = -1;
            return -1;
        }
        got += (size_t)n;
    }
    uint32_t rlen = 0;
    memcpy(&rlen, hdr + 6, 4);

    size_t read_tot = 0;
    while (read_tot < rlen) {
        char dump[1024];
        size_t chunk = rlen - read_tot;
        ssize_t n;
        if (out && read_tot + 1 < out_sz) {
            size_t avail = out_sz - 1 - read_tot;
            if (chunk > avail)
                chunk = avail;
            n = recv(sway_fd, out + read_tot, chunk, 0);
        } else {
            if (chunk > sizeof(dump))
                chunk = sizeof(dump);
            n = recv(sway_fd, dump, chunk, 0);
        }
        if (n <= 0) {
            close(sway_fd);
            sway_fd = -1;
            return -1;
        }
        read_tot += (size_t)n;
    }
    if (out && out_sz > 0) {
        size_t end = read_tot < out_sz ? read_tot : out_sz - 1;
        out[end] = '\0';
    }
    return 0;
}

static bool sway_parse_node_around(const char *tree, const char *hit,
                                   long *id_out) {
    const char *p = hit;
    int depth = 0;
    while (p > tree) {
        p--;
        if (*p == '}')
            depth++;
        else if (*p == '{') {
            if (depth == 0)
                break;
            depth--;
        }
    }
    if (*p != '{')
        return false;

    const char *obj_end = p + 1;
    depth = 1;
    bool in_str = false;
    while (*obj_end && depth > 0) {
        if (*obj_end == '"' && obj_end[-1] != '\\')
            in_str = !in_str;
        else if (!in_str) {
            if (*obj_end == '{')
                depth++;
            else if (*obj_end == '}')
                depth--;
        }
        obj_end++;
    }

    size_t span = (size_t)(obj_end - p);
    if (span >= 8192)
        span = 8191;
    char node[8192];
    memcpy(node, p, span);
    node[span] = '\0';

    if (id_out) {
        const char *idp = strstr(node, "\"id\":");
        if (idp)
            *id_out = strtol(skip_ws(idp + 5), NULL, 10);
    }

    const char *rp = strstr(node, "\"rect\":");
    if (rp) {
        json_int(rp, "\"x\":", &win_x);
        json_int(rp, "\"y\":", &win_y);
        json_int(rp, "\"width\":", &win_w);
        json_int(rp, "\"height\":", &win_h);
    }

    char fbuf[32];
    if (json_str(node, "\"floating\":", fbuf, sizeof(fbuf))) {
        win_floating = (strstr(fbuf, "on") != NULL);
    }
    return rp != NULL;
}

static bool hypr_poll_window(void) {
    char buf[2048];
    if (hypr_req("j/activewindow", buf, sizeof(buf)) <= 0)
        return false;

    char addr[64] = {0};
    if (json_str(buf, "\"address\":", addr, sizeof(addr)) &&
        strcmp(addr, hypr_addr) == 0) {
        json_pair(buf, "\"at\":", &win_x, &win_y);
        json_pair(buf, "\"size\":", &win_w, &win_h);
        json_bool(buf, "\"floating\":", &win_floating);
        return true;
    }

    static char cbuf[65536];
    if (hypr_req("j/clients", cbuf, sizeof(cbuf)) <= 0)
        return false;
    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", hypr_addr);
    const char *hit = strstr(cbuf, needle);
    if (!hit)
        return false;

    const char *p = hit;
    int depth = 0;
    while (p > cbuf) {
        p--;
        if (*p == '}')
            depth++;
        else if (*p == '{') {
            if (depth == 0)
                break;
            depth--;
        }
    }
    json_pair(p, "\"at\":", &win_x, &win_y);
    json_pair(p, "\"size\":", &win_w, &win_h);
    json_bool(p, "\"floating\":", &win_floating);
    return true;
}

static bool sway_poll_window(void) {
    static char tbuf[131072];
    if (sway_req(4, NULL, tbuf, sizeof(tbuf)) < 0)
        return false;
    char needle[48];
    snprintf(needle, sizeof(needle), "\"id\": %ld", sway_con_id);
    const char *hit = strstr(tbuf, needle);
    if (!hit) {
        snprintf(needle, sizeof(needle), "\"id\":%ld", sway_con_id);
        hit = strstr(tbuf, needle);
    }
    if (!hit)
        return false;
    return sway_parse_node_around(tbuf, hit, NULL);
}

static int stdin_wait_ms(int ms) {
    struct timeval tv = {0, ms * 1000};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static void posix_pump_input(void) {
    while (stdin_wait_ms(0)) {
        unsigned char c = 0;
        if (read(STDIN_FILENO, &c, 1) <= 0)
            break;
        if (c != 27) {
            key_push(c);
            continue;
        }
        if (!stdin_wait_ms(4)) {
            key_push(27);
            continue;
        }
        unsigned char s0 = 0;
        if (read(STDIN_FILENO, &s0, 1) <= 0) {
            key_push(27);
            continue;
        }
        if (s0 == '[') {
            if (!stdin_wait_ms(4))
                continue;
            unsigned char s1 = 0;
            if (read(STDIN_FILENO, &s1, 1) <= 0)
                continue;

            if (s1 == '<') {
                char seq[48];
                int idx = 0;
                char term = 0;
                while (idx < 47 && stdin_wait_ms(4)) {
                    unsigned char ch = 0;
                    if (read(STDIN_FILENO, &ch, 1) <= 0)
                        break;
                    if (ch == 'M' || ch == 'm') {
                        term = (char)ch;
                        break;
                    }
                    seq[idx++] = (char)ch;
                }
                seq[idx] = '\0';
                int btn = 0, col = 0, row = 0;
                if (term && sscanf(seq, "%d;%d;%d", &btn, &col, &row) == 3) {
                    posix_mx = col * 9;
                    posix_my = row * 18;
                    int b = btn & 3;
                    if (term == 'M') {
                        if (b == 0)
                            posix_lmb = true;
                        else if (b == 2)
                            posix_rmb = true;
                    } else {
                        if (b == 0)
                            posix_lmb = false;
                        else if (b == 2)
                            posix_rmb = false;
                    }
                }
                continue;
            }

            if (s1 >= '0' && s1 <= '9') {
                char seq[48];
                seq[0] = (char)s1;
                int idx = 1;
                char term = 0;
                while (idx < 47 && stdin_wait_ms(4)) {
                    unsigned char ch = 0;
                    if (read(STDIN_FILENO, &ch, 1) <= 0)
                        break;
                    if (ch >= '@' && ch <= '~') {
                        term = (char)ch;
                        break;
                    }
                    seq[idx++] = (char)ch;
                }
                seq[idx] = '\0';
                if (term == 't') {
                    int wx = 0, wy = 0;
                    if (sscanf(seq, "3;%d;%d", &wx, &wy) == 2) {
                        xtwin_x = wx;
                        xtwin_y = wy;
                        has_xtwinops = true;
                    }
                }
                continue;
            }

            switch (s1) {
            case 'D':
                key_push(KEY_LEFT);
                break;
            case 'C':
                key_push(KEY_RIGHT);
                break;
            case 'A':
                key_push(KEY_UP);
                break;
            case 'B':
                key_push(KEY_DOWN);
                break;
            }
        } else if (s0 == 'O') {
            if (!stdin_wait_ms(4))
                continue;
            unsigned char s1 = 0;
            if (read(STDIN_FILENO, &s1, 1) <= 0)
                continue;
            switch (s1) {
            case 'D':
                key_push(KEY_LEFT);
                break;
            case 'C':
                key_push(KEY_RIGHT);
                break;
            case 'A':
                key_push(KEY_UP);
                break;
            case 'B':
                key_push(KEY_DOWN);
                break;
            }
        }
    }
}

static void wm_init(void) {
    const char *hsig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (hsig && *hsig) {
        const char *xdg = getenv("XDG_RUNTIME_DIR");
        if (xdg && *xdg)
            snprintf(hypr_sock, sizeof(hypr_sock), "%s/hypr/%s/.socket.sock",
                     xdg, hsig);
        else
            snprintf(hypr_sock, sizeof(hypr_sock), "/tmp/hypr/%s/.socket.sock",
                     hsig);

        char buf[2048];
        if (hypr_req("j/activewindow", buf, sizeof(buf)) > 0 &&
            json_str(buf, "\"address\":", hypr_addr, sizeof(hypr_addr))) {
            wm_type = WM_HYPR;
            json_pair(buf, "\"at\":", &win_x, &win_y);
            json_pair(buf, "\"size\":", &win_w, &win_h);
            json_bool(buf, "\"floating\":", &win_floating);
            return;
        }
    }

    const char *ssock = getenv("SWAYSOCK");
    if (!ssock || !*ssock)
        ssock = getenv("I3SOCK");
    if (ssock && *ssock) {
        sway_fd = unix_connect(ssock);
        if (sway_fd >= 0) {
            static char tbuf[131072];
            if (sway_req(4, NULL, tbuf, sizeof(tbuf)) == 0) {
                const char *hit = strstr(tbuf, "\"focused\": true");
                if (!hit)
                    hit = strstr(tbuf, "\"focused\":true");
                if (hit && sway_parse_node_around(tbuf, hit, &sway_con_id) &&
                    sway_con_id >= 0) {
                    wm_type = WM_SWAY;
                    return;
                }
            }
            close(sway_fd);
            sway_fd = -1;
        }
    }

    printf("\033[13t");
    fflush(stdout);
    if (stdin_wait_ms(15))
        posix_pump_input();
}

static inline void sys_init(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\033[?25l\033[?1002h\033[?1006h");
    fflush(stdout);

    wm_init();
}

static inline void sys_cleanup(void) {
    if (sway_fd >= 0) {
        close(sway_fd);
        sway_fd = -1;
    }
    printf("\033[?1006l\033[?1002l\033[?25h\033[0m\n");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static inline double sys_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

static inline void sys_get_cursor_pos(int *x, int *y) {
    posix_pump_input();
    if (wm_type == WM_HYPR && (posix_rmb || posix_lmb)) {
        char buf[256];
        if (hypr_req("j/cursorpos", buf, sizeof(buf)) > 0) {
            int gx = 0, gy = 0;
            if (json_int(buf, "\"x\":", &gx) && json_int(buf, "\"y\":", &gy)) {
                *x = gx;
                *y = gy;
                return;
            }
        }
    }
    *x = posix_mx;
    *y = posix_my;
}

static inline bool sys_get_window_center(int *x, int *y) {
    if (wm_type == WM_HYPR && hypr_poll_window()) {
        *x = win_x + win_w / 2;
        *y = win_y + win_h / 2;
        return true;
    }
    if (wm_type == WM_SWAY && sway_poll_window()) {
        *x = win_x + win_w / 2;
        *y = win_y + win_h / 2;
        return true;
    }
    if (has_xtwinops) {
        printf("\033[13t");
        fflush(stdout);
        posix_pump_input();
        *x = xtwin_x;
        *y = xtwin_y;
        return true;
    }
    return false;
}

static inline bool sys_window_drag_update(void) {
    posix_pump_input();
    if (!posix_rmb || !win_floating || wm_type == WM_NONE) {
        drag_active = false;
        return false;
    }

    int cx = 0, cy = 0;
    sys_get_cursor_pos(&cx, &cy);

    if (!drag_active) {
        drag_active = true;
        drag_cur0_x = cx;
        drag_cur0_y = cy;
        drag_win0_x = win_x;
        drag_win0_y = win_y;
    }

    int nx = drag_win0_x + (cx - drag_cur0_x);
    int ny = drag_win0_y + (cy - drag_cur0_y);

    if (wm_type == WM_HYPR) {
        char cmd[128], rep[64];
        snprintf(cmd, sizeof(cmd),
                 "dispatch movewindowpixel exact %d %d,address:%s", nx, ny,
                 hypr_addr);
        hypr_req(cmd, rep, sizeof(rep));
        return true;
    }
    if (wm_type == WM_SWAY) {
        char cmd[128], rep[128];
        snprintf(cmd, sizeof(cmd),
                 "[con_id=%ld] move absolute position %d px %d px", sway_con_id,
                 nx, ny);
        sway_req(0, cmd, rep, sizeof(rep));
        return true;
    }
    return false;
}

static inline bool sys_is_lbutton_down(void) {
    posix_pump_input();
    return posix_lmb || (posix_rmb && (!win_floating || wm_type == WM_NONE));
}

static inline void sys_get_term_size(int *cols, int *rows) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 &&
        w.ws_row > 0) {
        *cols = w.ws_col;
        *rows = w.ws_row;
    }
}

static inline int sys_kbhit(void) {
    posix_pump_input();
    return key_head != key_tail;
}

static inline int sys_getch(void) {
    posix_pump_input();
    if (key_head == key_tail)
        return 0;
    int k = key_queue[key_tail];
    key_tail = (key_tail + 1) & 63;
    return k;
}

static inline void sys_sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }

#endif
