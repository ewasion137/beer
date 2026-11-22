#define _DEFAULT_SOURCE
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#ifdef _WIN32
#include "wininclude.h"
#else
#include "posixinclude.h"
#endif

#include "czech.h"
#include "drink.h"
#include "german.h"
#include "irish.h"
#include "kvass.h"


#define MAX_COLS 512
#define MAX_ROWS 200
#define MAX_BUBBLES 240
#define MAX_DROPS 256
#define N_SITES 12
#define BUFFER_SIZE (4 * 1024 * 1024)

typedef struct {
    Col fg, bg;
    char g[5];
} Cell;

static const Col COL_DEF = {-1, -1, -1};
static Cell grid[MAX_ROWS][MAX_COLS];
static Cell prev_grid[MAX_ROWS][MAX_COLS];
static const char *bubmap[MAX_ROWS][MAX_COLS];

static const char *cava_blocks[9] = {" ",
                                     "\xE2\x96\x81",
                                     "\xE2\x96\x82",
                                     "\xE2\x96\x83",
                                     "\xE2\x96\x84",
                                     "\xE2\x96\x85",
                                     "\xE2\x96\x86",
                                     "\xE2\x96\x87",
                                     "\xE2\x96\x88"};

static int clampi(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
static Col rgb(int r, int g, int b) {
    Col c = {(short)clampi(r), (short)clampi(g), (short)clampi(b)};
    return c;
}
static Col rgb_q(int r, int g, int b) { return rgb(r & ~3, g & ~3, b & ~3); }

static Col lerp_col(Col a, Col b, float t) {
    return rgb((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t),
               (int)(a.b + (b.b - a.b) * t));
}

static Col shade(Col c, int d) {
    return c.r < 0 ? c : rgb_q(c.r + d, c.g + d, c.b + d);
}
static bool col_eq(Col a, Col b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static bool cell_eq(const Cell *a, const Cell *b) {
    if (strcmp(a->g, b->g) != 0)
        return false;
    if (!col_eq(a->bg, b->bg))
        return false;
    if (a->g[0] == ' ' && a->g[1] == '\0')
        return true;
    return col_eq(a->fg, b->fg);
}

static void set_cell(int x, int y, Col fg, Col bg, const char *g, int cols,
                     int rows) {
    if (x < 0 || y < 0 || x >= cols || y >= rows)
        return;
    Cell *c = &grid[y][x];
    c->fg = fg;
    c->bg = bg;
    size_t n = strlen(g);
    if (n > 4)
        n = 4;
    memcpy(c->g, g, n);
    c->g[n] = 0;
}

static void put_text(int x, int y, Col fg, Col bg, const char *s, int cols,
                     int rows) {
    while (*s && x < cols) {
        unsigned char ch = (unsigned char)*s;
        int len = ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4;
        char tmp[5] = {0};
        for (int k = 0; k < len && s[k]; k++)
            tmp[k] = s[k];
        set_cell(x++, y, fg, bg, tmp, cols, rows);
        s += len;
    }
}

static unsigned hash2(int x, int y, int t) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u +
                 (unsigned)t * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static char frame_buf[BUFFER_SIZE];
static int buf_len = 0;

static void buf_write(const char *str) {
    while (*str && buf_len < BUFFER_SIZE - 1)
        frame_buf[buf_len++] = *str++;
}

static void flush_grid(int cols, int rows, bool full_redraw) {
    buf_len = 0;
    if (full_redraw)
        buf_write("\033[2J");

    Col lf = {-2, 0, 0}, lb = {-2, 0, 0};
    int cur_x = -1, cur_y = -1;
    char tmp[96];

    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            Cell *c = &grid[y][x];
            Cell *p = &prev_grid[y][x];
            if (!full_redraw && cell_eq(c, p))
                continue;

            if (cur_y != y || cur_x != x || cur_x >= cols) {
                snprintf(tmp, sizeof(tmp), "\033[%d;%dH", y + 1, x + 1);
                buf_write(tmp);
            }

            bool is_space = (c->g[0] == ' ' && c->g[1] == '\0');
            bool need_fg = !is_space && !col_eq(c->fg, lf);
            bool need_bg = !col_eq(c->bg, lb);

            if (need_fg || need_bg) {
                if (need_fg && need_bg) {
                    char f[24], b[24];
                    if (c->fg.r < 0)
                        strcpy(f, "39");
                    else
                        snprintf(f, sizeof(f), "38;2;%d;%d;%d", c->fg.r,
                                 c->fg.g, c->fg.b);
                    if (c->bg.r < 0)
                        strcpy(b, "49");
                    else
                        snprintf(b, sizeof(b), "48;2;%d;%d;%d", c->bg.r,
                                 c->bg.g, c->bg.b);
                    snprintf(tmp, sizeof(tmp), "\033[%s;%sm", f, b);
                    lf = c->fg;
                    lb = c->bg;
                } else if (need_fg) {
                    if (c->fg.r < 0)
                        strcpy(tmp, "\033[39m");
                    else
                        snprintf(tmp, sizeof(tmp), "\033[38;2;%d;%d;%dm",
                                 c->fg.r, c->fg.g, c->fg.b);
                    lf = c->fg;
                } else {
                    if (c->bg.r < 0)
                        strcpy(tmp, "\033[49m");
                    else
                        snprintf(tmp, sizeof(tmp), "\033[48;2;%d;%d;%dm",
                                 c->bg.r, c->bg.g, c->bg.b);
                    lb = c->bg;
                }
                buf_write(tmp);
            }

            buf_write(c->g);
            *p = *c;
            cur_x = x + 1;
            cur_y = y;
        }
    }

    if (buf_len > 0) {
        buf_write("\033[0m");
        fwrite(frame_buf, 1, (size_t)buf_len, stdout);
        fflush(stdout);
    }
}

typedef struct {
    float x, y, vy, phase;
    bool active;
} Bubble;
typedef struct {
    float x, y, vx, vy;
    bool is_foam, active;
} Drop;

static Bubble bubbles[MAX_BUBBLES];
static Drop drops[MAX_DROPS];
static volatile sig_atomic_t running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

static float fluid_h[MAX_COLS];
static float fluid_u[MAX_COLS + 1];
static float foam_col[MAX_COLS];

static void spawn_drop(float x, float y, float vx, float vy, bool is_foam) {
    for (int i = 0; i < MAX_DROPS; i++) {
        if (drops[i].active)
            continue;
        drops[i].active = true;
        drops[i].x = x;
        drops[i].y = y;
        drops[i].vx = vx;
        drops[i].vy = vy;
        drops[i].is_foam = is_foam;
        return;
    }
}

static float ov(float a, float b, float y0, float y1) {
    float lo = a > y0 ? a : y0, hi = b < y1 ? b : y1;
    return hi > lo ? hi - lo : 0.0f;
}

static void reset_fluid(float foam_val) {
    memset(fluid_h, 0, sizeof(fluid_h));
    memset(fluid_u, 0, sizeof(fluid_u));
    for (int i = 0; i < MAX_COLS; i++)
        foam_col[i] = foam_val;
    for (int i = 0; i < MAX_BUBBLES; i++)
        bubbles[i].active = false;
}

int main(void) {
    signal(SIGINT, handle_sigint);
    sys_init();
    srand((unsigned int)time(NULL));
    printf("\033[?1049h\033[2J");
    fflush(stdout);

    const Drink *drinks[4] = {&DRINK_IRISH, &DRINK_GERMAN, &DRINK_CZECH,
                              &DRINK_KVASS};
    int drink_idx = 2;
    const Drink *dr = drinks[drink_idx];

    const float site_frac[N_SITES] = {0.04f, 0.11f, 0.19f, 0.28f, 0.37f, 0.46f,
                                      0.54f, 0.63f, 0.72f, 0.81f, 0.89f, 0.96f};

    float level = 0.72f, target = 0.72f;
    bool pouring = false;
    unsigned frame = 0;

    reset_fluid(dr->foam_init);
    for (int i = 0; i < MAX_DROPS; i++)
        drops[i].active = false;

    int prev_mx = 0, prev_my = 0;
    sys_get_cursor_pos(&prev_mx, &prev_my);

    int prev_wx = 0, prev_wy = 0;
    bool prev_win_valid = sys_get_window_center(&prev_wx, &prev_wy);
    bool was_mouse_active = false;

    float filt_vx = 0.0f, filt_vy = 0.0f;
    float prev_fvx = 0.0f, prev_fvy = 0.0f;
    float ax_s1 = 0.0f, ay_s1 = 0.0f;
    float ax_lag = 0.0f, ay_lag = 0.0f;
    float vert_off = 0.0f, vert_vel = 0.0f;
    float key_ax = 0.0f, key_ay = 0.0f;

    double last_time = sys_time_sec();
    int last_cols = 0, last_rows = 0, prev_n = 0;

    while (running) {
        double now = sys_time_sec();
        float dt = clampf((float)(now - last_time), 0.002f, 0.05f);
        last_time = now;
        float step_scale = dt * 60.0f;

        int cols = 80, rows = 24;
        sys_get_term_size(&cols, &rows);
        if (cols > MAX_COLS - 1)
            cols = MAX_COLS - 1;
        if (rows > MAX_ROWS - 1)
            rows = MAX_ROWS - 1;

        bool full_redraw = false;
        if (cols != last_cols || rows != last_rows) {
            full_redraw = true;
            last_cols = cols;
            last_rows = rows;
        }

        int ocean_top = 1;
        int ocean_bottom = rows;
        int n = cols;
        int inner_h = ocean_bottom - ocean_top;
        float inner_bot = (float)ocean_bottom;

        if (n <= 8 || inner_h <= 4) {
            sys_sleep_ms(20);
            continue;
        }

        if (prev_n > 0 && n > prev_n) {
            for (int i = prev_n; i < n; i++) {
                fluid_h[i] = 0.0f;
                fluid_u[i] = 0.0f;
                foam_col[i] = foam_col[prev_n - 1];
            }
            fluid_u[n] = 0.0f;
        }
        prev_n = n;

        frame++;

        int cur_mx = 0, cur_my = 0;
        sys_get_cursor_pos(&cur_mx, &cur_my);
        int mouse_dx = cur_mx - prev_mx;
        int mouse_dy = cur_my - prev_my;
        prev_mx = cur_mx;
        prev_my = cur_my;

        bool dragging = sys_window_drag_update();
        bool shaking = !dragging && sys_is_lbutton_down();
        bool mouse_active = dragging || shaking;
        if (mouse_active && !was_mouse_active) {
            mouse_dx = 0;
            mouse_dy = 0;
        }
        was_mouse_active = mouse_active;

        int cur_wx = 0, cur_wy = 0;
        bool has_win = sys_get_window_center(&cur_wx, &cur_wy);
        int win_dx = (has_win && prev_win_valid) ? (cur_wx - prev_wx) : 0;
        int win_dy = (has_win && prev_win_valid) ? (cur_wy - prev_wy) : 0;
        if (has_win) {
            prev_wx = cur_wx;
            prev_wy = cur_wy;
            prev_win_valid = true;
        }

        float drive_x = mouse_active ? (float)mouse_dx : (float)win_dx;
        float drive_y = mouse_active ? (float)mouse_dy : (float)win_dy;

        float raw_vx = drive_x / fmaxf(step_scale, 0.15f);
        float raw_vy = drive_y / fmaxf(step_scale, 0.15f);

        float v_alpha = 1.0f - expf(-dt * 45.0f);
        filt_vx += v_alpha * (raw_vx - filt_vx);
        filt_vy += v_alpha * (raw_vy - filt_vy);

        float raw_ax =
            clampf((filt_vx - prev_fvx) / fmaxf(step_scale, 0.15f) + key_ax,
                   -45.0f, 45.0f);
        float raw_ay =
            clampf((filt_vy - prev_fvy) / fmaxf(step_scale, 0.15f) + key_ay,
                   -45.0f, 45.0f);
        prev_fvx = filt_vx;
        prev_fvy = filt_vy;
        key_ax *= expf(-dt * 12.0f);
        key_ay *= expf(-dt * 12.0f);

        float lag_alpha = 1.0f - expf(-dt * 42.0f);
        ax_s1 += lag_alpha * (raw_ax - ax_s1);
        ay_s1 += lag_alpha * (raw_ay - ay_s1);
        ax_lag += lag_alpha * (ax_s1 - ax_lag);
        ay_lag += lag_alpha * (ay_s1 - ay_lag);

        while (sys_kbhit()) {
            int ch = sys_getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) {
                running = 0;
            } else if (ch >= '1' && ch <= '4') {
                drink_idx = ch - '1';
                dr = drinks[drink_idx];
                if (level > 0.02f) {
                    for (int i = 0; i < n; i++) {
                        foam_col[i] =
                            clampf(foam_col[i] * 0.4f + dr->foam_init * 0.6f,
                                   0.0f, dr->foam_max);
                    }
                }
            } else if (ch == '\t') {
                drink_idx = (drink_idx + 1) & 3;
                dr = drinks[drink_idx];
                if (level > 0.02f) {
                    for (int i = 0; i < n; i++) {
                        foam_col[i] =
                            clampf(foam_col[i] * 0.4f + dr->foam_init * 0.6f,
                                   0.0f, dr->foam_max);
                    }
                }
            } else if (ch == ' ') {
                if (target > 0.0f) {
                    target = fmaxf(0.0f, target - 0.05f);
                    fluid_h[n / 2] -= 0.4f;
                    for (int i = 0; i < n; i++)
                        foam_col[i] = fmaxf(0.0f, foam_col[i] - 0.05f);
                }
            } else if (ch == 'r' || ch == 'R') {
                target = 0.74f;
                pouring = true;
            } else if (ch == KEY_LEFT) {
                key_ax -= 14.0f;
            } else if (ch == KEY_RIGHT) {
                key_ax += 14.0f;
            } else if (ch == KEY_UP) {
                key_ay -= 14.0f;
            } else if (ch == KEY_DOWN) {
                key_ay += 14.0f;
            }
        }

        if (level < target) {
            level += 0.003f * step_scale;
            if (pouring) {
                fluid_h[n / 2] -= 0.08f * step_scale;
                foam_col[n / 2] += dr->foam_agitate * 8.0f * step_scale;
                if ((rand() % 100) < 40) {
                    float sx = (float)(n / 2) +
                               (((float)(rand() % 100) / 50.0f) - 1.0f) * 1.5f;
                    float sy = inner_bot - level * (float)inner_h;
                    float svx = (((float)(rand() % 100) / 50.0f) - 1.0f) * 0.9f;
                    float svy =
                        -0.55f - ((float)(rand() % 100) / 100.0f) * 0.65f;
                    spawn_drop(sx, sy, svx, svy, (rand() % 2) == 0);
                }
            }
            if (level >= target) {
                level = target;
                pouring = false;
            }
        } else if (level > target) {
            level -= 0.0025f * step_scale;
            if (level < target)
                level = target;
            pouring = false;
        }

        bool is_empty = (level <= 0.002f && !pouring);
        if (is_empty) {
            level = 0.0f;
            vert_off = 0.0f;
            vert_vel = 0.0f;
            reset_fluid(0.0f);
        }

        float eff_ax = ax_lag * dr->accel_gain;
        float eff_ay = ay_lag * dr->accel_gain;
        float agitation =
            (fabsf(eff_ax) + fabsf(eff_ay)) * dr->foam_agitate * step_scale;
        float g_base = dr->g_base;
        float g_eff =
            clampf(g_base - eff_ay * 0.045f, 0.10f * g_base, 3.2f * g_base);
        float eff_depth =
            fminf(14.0f, fmaxf(0.8f, level * (float)inner_h * 0.45f));

        int substeps = (int)ceilf(step_scale * 10.0f);
        if (substeps < 8)
            substeps = 8;
        if (substeps > 32)
            substeps = 32;
        float dt_sub = step_scale / (float)substeps;

        if (!is_empty) {
            float prev_vert_vel = vert_vel;
            for (int s = 0; s < substeps; s++) {
                float k_vert = vert_off > 0.0f ? 0.35f : 0.14f;
                vert_vel +=
                    (-k_vert * vert_off - 0.14f * vert_vel - eff_ay * 0.045f) *
                    dt_sub;
                vert_off += vert_vel * dt_sub;
                vert_off = clampf(vert_off, -2.5f, 1.0f);

                float rebound = (prev_vert_vel < -0.15f && vert_vel >= -0.15f)
                                    ? fabsf(prev_vert_vel) * 0.4f
                                    : 0.0f;
                prev_vert_vel = vert_vel;

                fluid_u[0] = 0.0f;
                fluid_u[n] = 0.0f;
                float ax_force = -eff_ax * 0.032f;

                for (int i = 1; i < n; i++) {
                    float visc =
                        dr->visc *
                        (fluid_u[i - 1] - 2.0f * fluid_u[i] + fluid_u[i + 1]);
                    fluid_u[i] +=
                        dt_sub * (-g_eff * (fluid_h[i] - fluid_h[i - 1]) +
                                  ax_force - dr->drag * fluid_u[i] + visc);
                    fluid_u[i] = clampf(fluid_u[i], -3.8f, 3.8f);
                }

                float ay_shear = eff_ay * 0.024f + rebound;
                float asym = 0.12f * sinf((float)frame * 0.23f);

                for (int i = 0; i < n; i++) {
                    float xnorm = ((float)i + 0.5f) / (float)n - 0.5f;
                    float profile =
                        (1.0f - 12.0f * xnorm * xnorm) + asym * xnorm;
                    float dh =
                        -dt_sub * eff_depth * (fluid_u[i + 1] - fluid_u[i]) +
                        dt_sub * ay_shear * profile;
                    fluid_h[i] += dh;

                    if (level > 0.05f) {
                        float rate = fabsf(dh) / fmaxf(dt_sub, 0.01f);
                        bool near_wall = (i <= 2 || i >= n - 3);
                        float thresh = near_wall ? dr->drop_thresh * 0.6f
                                                 : dr->drop_thresh;
                        int chance =
                            near_wall ? dr->drop_chance : (dr->drop_chance / 2);
                        if ((rate > thresh || rebound > 0.08f) &&
                            (rand() % 100) < chance) {
                            float sy = inner_bot - level * (float)inner_h +
                                       vert_off - fluid_h[i];
                            float svx =
                                fluid_u[i] * 0.22f +
                                (((float)(rand() % 100) / 50.0f) - 1.0f) *
                                    0.55f;
                            if (i <= 2)
                                svx = fabsf(svx) + 0.2f;
                            else if (i >= n - 3)
                                svx = -fabsf(svx) - 0.2f;
                            float svy =
                                -0.45f - clampf(rate * 0.9f + rebound * 1.5f,
                                                0.1f, 1.4f);
                            spawn_drop((float)i + 0.5f, sy, svx, svy,
                                       (rand() % 3) == 0);
                        }
                    }
                }
            }

            float mean_h = 0.0f;
            for (int i = 0; i < n; i++)
                mean_h += fluid_h[i];
            mean_h /= (float)n;
            float h_lim = (float)inner_h * 0.55f;
            for (int i = 0; i < n; i++)
                fluid_h[i] = clampf(fluid_h[i] - mean_h, -h_lim, h_lim);

            for (int i = 1; i < n; i++) {
                float flux =
                    fluid_u[i] * step_scale * dr->foam_advect *
                    (fluid_u[i] > 0.0f ? foam_col[i - 1] : foam_col[i]);
                float max_f =
                    0.40f * (fluid_u[i] > 0.0f ? foam_col[i - 1] : foam_col[i]);
                flux = clampf(flux, -max_f, max_f);
                foam_col[i - 1] -= flux;
                foam_col[i] += flux;
            }
            for (int i = 1; i < n; i++) {
                float diff =
                    0.04f * step_scale * (foam_col[i - 1] - foam_col[i]);
                foam_col[i - 1] -= diff;
                foam_col[i] += diff;
            }
            float fcap = fminf(dr->foam_max, 0.26f * (float)inner_h);
            for (int i = 0; i < n; i++) {
                foam_col[i] += agitation *
                               (0.6f + 0.8f * ((float)(rand() % 100) / 100.0f));
                if (foam_col[i] > dr->foam_base)
                    foam_col[i] -= dr->foam_decay * step_scale;
                foam_col[i] = clampf(foam_col[i], 0.0f, fcap);
            }
        }

        static float surf[MAX_COLS], ftop[MAX_COLS];
        float base = inner_bot - level * (float)inner_h;
        for (int i = 0; i < n; i++) {
            if (is_empty) {
                surf[i] = inner_bot;
                ftop[i] = inner_bot;
            } else {
                float l = fluid_h[i > 0 ? i - 1 : i],
                      r = fluid_h[i < n - 1 ? i + 1 : i];
                float sm = (l + fluid_h[i] * 2.0f + r) * 0.25f;
                surf[i] = base + vert_off - sm;
                if (surf[i] > inner_bot)
                    surf[i] = inner_bot;
                ftop[i] =
                    surf[i] -
                    foam_col[i] * (0.9f + 0.15f * sinf((float)i * 0.7f +
                                                       (float)frame * 0.04f));
            }
        }

        if (!is_empty && level > 0.05f) {
            int spawn_chance =
                dr->bubble_rate +
                (int)clampf((fabsf(eff_ax) + fabsf(eff_ay)) * 3.5f, 0.0f,
                            65.0f);
            for (int k = 0; k < 3; k++) {
                if ((rand() % 100) >= spawn_chance)
                    continue;
                for (int i = 0; i < MAX_BUBBLES; i++) {
                    if (bubbles[i].active)
                        continue;
                    bubbles[i].active = true;
                    bubbles[i].x = site_frac[rand() % N_SITES] * (float)n;
                    bubbles[i].y = inner_bot - 0.3f;
                    bubbles[i].vy = (0.04f + (float)(rand() % 45) / 1000.0f) *
                                    dr->bubble_speed;
                    bubbles[i].phase = (float)(rand() % 628) / 100.0f;
                    break;
                }
            }
        }

        float g_ratio = g_eff / g_base;
        memset(bubmap, 0, sizeof(bubmap));
        for (int i = 0; i < MAX_BUBBLES; i++) {
            Bubble *b = &bubbles[i];
            if (!b->active)
                continue;
            int ci = (int)b->x;
            ci = ci < 0 ? 0 : ci >= n ? n - 1 : ci;
            float u_loc = 0.5f * (fluid_u[ci] + fluid_u[ci + 1]);

            b->x += u_loc * 0.12f * step_scale;
            b->x = clampf(b->x, 0.5f, (float)n - 0.5f);
            b->y -= b->vy * clampf(g_ratio, 0.25f, 2.2f) * step_scale;
            b->vy = fminf(b->vy * (1.0f + 0.012f * step_scale),
                          0.50f * dr->bubble_speed);
            b->phase += 0.15f * step_scale;

            if (b->y < surf[ci] + 0.3f) {
                b->active = false;
                foam_col[ci] += 0.012f;
                continue;
            }
            int bx = (int)(b->x + sinf(b->phase) * 0.35f), by = (int)b->y;
            if (bx >= 0 && bx < n && by >= ocean_top && by < ocean_bottom) {
                if (dr->fine_bubbles) {
                    bubmap[by][bx] = b->vy < 0.15f ? "\xC2\xB7" : "\xC2\xB0";
                } else {
                    bubmap[by][bx] = b->vy < 0.14f   ? "\xC2\xB7"
                                     : b->vy < 0.30f ? "\xC2\xB0"
                                                     : "o";
                }
            }
        }

        for (int i = 0; i < MAX_DROPS; i++) {
            Drop *d = &drops[i];
            if (!d->active)
                continue;
            d->vy += 0.068f * step_scale;
            d->x += d->vx * step_scale;
            d->y += d->vy * step_scale;
            if (d->x <= 0.0f) {
                d->x = 0.0f;
                d->vx = fabsf(d->vx) * 0.55f;
            } else if (d->x >= (float)n - 0.1f) {
                d->x = (float)n - 0.1f;
                d->vx = -fabsf(d->vx) * 0.55f;
            }
            int ci = (int)d->x;
            ci = ci < 0 ? 0 : ci >= n ? n - 1 : ci;
            if (d->y >= surf[ci]) {
                d->active = false;
                if (!is_empty) {
                    fluid_h[ci] -= 0.06f;
                    foam_col[ci] += 0.03f;
                }
            } else if (d->y < (float)ocean_top) {
                d->y = (float)ocean_top;
                d->vy = fabsf(d->vy) * 0.35f;
            }
        }

        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++)
                set_cell(x, y, COL_DEF, COL_DEF, " ", cols, rows);

        Col hdr_fg = rgb(30, 30, 30), hdr_bg = rgb(235, 235, 235);
        for (int x = 0; x < cols; x++)
            set_cell(x, 0, hdr_fg, hdr_bg, " ", cols, rows);
        put_text(1, 0, hdr_fg, hdr_bg,
                 "[SPACE] Drink | [RMB] Move | [LMB] Shake | [1-4] Sort | [R] "
                 "Refill | [Q] Quit",
                 cols, rows);

        char status[48];
        snprintf(status, sizeof(status), "%s: %3d%%", dr->name,
                 (int)(level * 100.0f + 0.5f));
        int slen = (int)strlen(status);
        if (cols > slen + 2)
            put_text(cols - slen - 1, 0, hdr_fg, hdr_bg, status, cols, rows);

        if (!is_empty) {
            for (int i = 0; i < n; i++) {
                float foam_th = surf[i] - ftop[i];

                for (int y = ocean_top; y < ocean_bottom; y++) {
                    float y0 = (float)y, y1 = (float)y + 1.0f;
                    float f = ov(ftop[i], surf[i], y0, y1);
                    float b = ov(surf[i], inner_bot, y0, y1);
                    float a = 1.0f - f - b;
                    if (a < 0.0f)
                        a = 0.0f;

                    Col lc[3];
                    float lw[3];
                    int lk[3];
                    int ln = 0;
                    if (a > 0.06f) {
                        lc[ln] = COL_DEF;
                        lk[ln] = 0;
                        lw[ln++] = a;
                    }
                    if (f > 0.06f) {
                        float fd = foam_th > 0.1f
                                       ? clampf((y0 + 0.5f - ftop[i]) / foam_th,
                                                0.0f, 1.0f)
                                       : 0.0f;
                        lc[ln] =
                            shade(lerp_col(dr->foam_hi, dr->foam_lo, fd), 0);
                        lk[ln] = 1;
                        lw[ln++] = f;
                    }
                    if (b > 0.06f) {
                        float d = clampf((y0 + 0.5f - surf[i]) / (float)inner_h,
                                         0.0f, 1.0f);
                        lc[ln] =
                            shade(lerp_col(dr->beer_hi, dr->beer_lo, d), 0);
                        lk[ln] = 2;
                        lw[ln++] = b;
                    }
                    if (ln == 0)
                        continue;

                    Col top = lc[0], bot = lc[ln - 1];
                    float frac = lw[ln - 1];
                    int lvl = ln == 1 ? 8 : (int)(frac * 8.0f + 0.5f);

                    if (ln > 1 && lvl > 0 && lvl < 8) {
                        set_cell(i, y, bot, top, cava_blocks[lvl], cols, rows);
                    } else {
                        bool use_bot = (ln == 1 || lvl >= 8);
                        Col solid = use_bot ? bot : top;
                        if (solid.r < 0)
                            continue;
                        bool is_foam = lk[use_bot ? ln - 1 : 0] == 1;
                        const char *gl = " ";
                        Col fg = COL_DEF;
                        if (is_foam) {
                            unsigned hv = hash2(i, y, (int)(frame / 30)) % 7;
                            if (hv == 0)
                                gl = "\xC2\xB7";
                            else if (hv == 1)
                                gl = "\xC2\xB0";
                            if (gl[0] != ' ')
                                fg = shade(solid, -20);
                        } else if (bubmap[y][i]) {
                            gl = bubmap[y][i];
                            fg = dr->bubble_col;
                        }
                        set_cell(i, y, fg, solid, gl, cols, rows);
                    }
                }
            }
        }

        for (int i = 0; i < MAX_DROPS; i++) {
            if (!drops[i].active)
                continue;
            int dx = (int)drops[i].x;
            int dy = (int)drops[i].y;
            if (dy >= ocean_top && dy < ocean_bottom && dx >= 0 && dx < n) {
                if (is_empty || (float)dy < ftop[dx]) {
                    Col dc =
                        drops[i].is_foam ? dr->foam_drop_col : dr->drop_col;
                    const char *dg =
                        drops[i].is_foam
                            ? ((fabsf(drops[i].vy) > 0.35f) ? "\xC2\xB0"
                                                            : "\xC2\xB7")
                            : ((fabsf(drops[i].vy) > 0.45f) ? "o"
                                                            : "\xE2\x80\xA2");
                    set_cell(dx, dy, dc, COL_DEF, dg, cols, rows);
                }
            }
        }

        if (pouring) {
            int sx = n / 2;
            for (int k = 0; k < 2; k++) {
                int stop = (int)surf[sx + k];
                for (int y = ocean_top; y < stop && y < rows; y++)
                    set_cell(sx + k, y, dr->bubble_col, dr->beer_hi,
                             ((y + (int)frame) & 1) ? "\xE2\x96\x91" : " ",
                             cols, rows);
            }
        }

        if (is_empty) {
            const char *msg = "EMPTY! [R]";
            put_text(n / 2 - 5, ocean_top + inner_h / 2, rgb(255, 90, 90),
                     COL_DEF, msg, cols, rows);
        }

        flush_grid(cols, rows, full_redraw);
        sys_sleep_ms(14);
    }

    printf("\033[?1049l");
    sys_cleanup();
    return 0;
}
