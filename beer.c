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

#define MAX_COLS 300
#define MAX_ROWS 120
#define MAX_BUBBLES 180
#define MAX_DROPS 64
#define N_SITES 9
#define SUBSTEPS 8
#define BUFFER_SIZE (2 * 1024 * 1024)

typedef struct {
    short r, g, b;
} Col;
typedef struct {
    Col fg, bg;
    char g[5];
} Cell;

static const Col COL_DEF = {-1, -1, -1};
static Cell grid[MAX_ROWS][MAX_COLS];
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

static Col lerp_col(Col a, Col b, float t) {
    return rgb((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t),
               (int)(a.b + (b.b - a.b) * t));
}

static Col shade(Col c, int d) {
    return c.r < 0 ? c : rgb(c.r + d, c.g + d, c.b + d);
}
static bool col_eq(Col a, Col b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
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

static void flush_grid(int cols, int rows) {
    buf_len = 0;
    Col lf = {-2, 0, 0}, lb = {-2, 0, 0};
    char tmp[96];
    for (int y = 0; y < rows; y++) {
        snprintf(tmp, sizeof(tmp), "\033[%d;1H", y + 1);
        buf_write(tmp);
        for (int x = 0; x < cols; x++) {
            Cell *c = &grid[y][x];
            if (!col_eq(c->fg, lf) || !col_eq(c->bg, lb)) {
                char f[32], b[32];
                if (c->fg.r < 0)
                    strcpy(f, "39");
                else
                    snprintf(f, sizeof(f), "38;2;%d;%d;%d", c->fg.r, c->fg.g,
                             c->fg.b);
                if (c->bg.r < 0)
                    strcpy(b, "49");
                else
                    snprintf(b, sizeof(b), "48;2;%d;%d;%d", c->bg.r, c->bg.g,
                             c->bg.b);
                snprintf(tmp, sizeof(tmp), "\033[%s;%sm", f, b);
                buf_write(tmp);
                lf = c->fg;
                lb = c->bg;
            }
            buf_write(c->g);
        }
    }
    buf_write("\033[0m");
    fwrite(frame_buf, 1, (size_t)buf_len, stdout);
    fflush(stdout);
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

int main(void) {
    signal(SIGINT, handle_sigint);
    sys_init();
    srand((unsigned int)time(NULL));
    printf("\033[?1049h\033[2J");
    fflush(stdout);

    const float site_frac[N_SITES] = {0.07f, 0.17f, 0.31f, 0.43f, 0.52f,
                                      0.64f, 0.75f, 0.86f, 0.94f};

    float level = 0.76f, target = 0.76f;
    bool pouring = false;
    unsigned frame = 0;

    for (int i = 0; i < MAX_COLS; i++)
        foam_col[i] = 0.9f;
    for (int i = 0; i < MAX_BUBBLES; i++)
        bubbles[i].active = false;
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
        if (cols > MAX_COLS - 2)
            cols = MAX_COLS - 2;
        if (rows > MAX_ROWS - 1)
            rows = MAX_ROWS - 1;
        if (cols != last_cols || rows != last_rows) {
            printf("\033[2J");
            last_cols = cols;
            last_rows = rows;
        }

        int glass_left = 6, glass_right = cols - 7;
        int glass_bottom = rows - 2, glass_top = 3;
        int n = glass_right - glass_left - 1;
        int inner_h = glass_bottom - glass_top;
        float inner_bot = (float)glass_bottom;

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
            } else if (ch == ' ') {
                if (target > 0.0f) {
                    target = fmaxf(0.0f, target - 0.05f);
                    fluid_h[n / 2] -= 0.4f;
                    for (int i = 0; i < n; i++)
                        foam_col[i] = fmaxf(0.0f, foam_col[i] - 0.05f);
                }
            } else if (ch == 'r' || ch == 'R') {
                target = 0.80f;
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
                foam_col[n / 2] += 0.03f * step_scale;
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

        float agitation =
            (fabsf(ax_lag) + fabsf(ay_lag)) * 0.0025f * step_scale;
        float g_base = 0.90f;
        float g_eff =
            clampf(g_base - ay_lag * 0.045f, 0.10f * g_base, 3.2f * g_base);
        float eff_depth = fmaxf(0.8f, level * (float)inner_h * 0.45f);
        float dt_sub = step_scale / (float)SUBSTEPS;

        float prev_vert_vel = vert_vel;
        for (int s = 0; s < SUBSTEPS; s++) {
            float k_vert = vert_off > 0.0f ? 0.35f : 0.14f;
            vert_vel +=
                (-k_vert * vert_off - 0.14f * vert_vel - ay_lag * 0.045f) *
                dt_sub;
            vert_off += vert_vel * dt_sub;
            vert_off = clampf(vert_off, -2.5f, 1.0f);

            float rebound = (prev_vert_vel < -0.15f && vert_vel >= -0.15f)
                                ? fabsf(prev_vert_vel) * 0.4f
                                : 0.0f;
            prev_vert_vel = vert_vel;

            fluid_u[0] = 0.0f;
            fluid_u[n] = 0.0f;
            float ax_force = -ax_lag * 0.032f;
            float drag = 0.030f;

            for (int i = 1; i < n; i++) {
                float visc = 0.06f * (fluid_u[i - 1] - 2.0f * fluid_u[i] +
                                      fluid_u[i + 1]);
                fluid_u[i] += dt_sub * (-g_eff * (fluid_h[i] - fluid_h[i - 1]) +
                                        ax_force - drag * fluid_u[i] + visc);
                fluid_u[i] = clampf(fluid_u[i], -3.5f, 3.5f);
            }

            float ay_shear = ay_lag * 0.024f + rebound;
            float asym = 0.12f * sinf((float)frame * 0.23f);

            for (int i = 0; i < n; i++) {
                float xnorm = ((float)i + 0.5f) / (float)n - 0.5f;
                float profile = (1.0f - 12.0f * xnorm * xnorm) + asym * xnorm;
                float dh = -dt_sub * eff_depth * (fluid_u[i + 1] - fluid_u[i]) +
                           dt_sub * ay_shear * profile;
                fluid_h[i] += dh;

                if (level > 0.05f && fabsf(dh) > 0.14f * dt_sub &&
                    (rand() % 100) < 18) {
                    float sx = (float)glass_left + 1.0f + (float)i + 0.5f;
                    float sy = inner_bot - level * (float)inner_h + vert_off -
                               fluid_h[i];
                    float svx =
                        fluid_u[i] * 0.15f +
                        (((float)(rand() % 100) / 50.0f) - 1.0f) * 0.25f;
                    float svy =
                        -0.35f - fabsf(dh / fmaxf(dt_sub, 0.01f)) * 0.65f;
                    spawn_drop(sx, sy, svx, svy, (rand() % 3) == 0);
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
            float flux = fluid_u[i] * step_scale * 0.025f *
                         (fluid_u[i] > 0.0f ? foam_col[i - 1] : foam_col[i]);
            float max_f =
                0.40f * (fluid_u[i] > 0.0f ? foam_col[i - 1] : foam_col[i]);
            flux = clampf(flux, -max_f, max_f);
            foam_col[i - 1] -= flux;
            foam_col[i] += flux;
        }
        for (int i = 1; i < n; i++) {
            float diff = 0.04f * step_scale * (foam_col[i - 1] - foam_col[i]);
            foam_col[i - 1] -= diff;
            foam_col[i] += diff;
        }
        for (int i = 0; i < n; i++) {
            if (level < 0.02f && !pouring) {
                foam_col[i] = 0.0f;
            } else {
                foam_col[i] += agitation *
                               (0.6f + 0.8f * ((float)(rand() % 100) / 100.0f));
                if (foam_col[i] > 0.45f)
                    foam_col[i] -= 0.0015f * step_scale;
                foam_col[i] = clampf(foam_col[i], 0.0f,
                                     fminf(4.2f, 0.22f * (float)inner_h));
            }
        }

        static float surf[MAX_COLS], ftop[MAX_COLS];
        float base = inner_bot - level * (float)inner_h;
        for (int i = 0; i < n; i++) {
            float l = fluid_h[i > 0 ? i - 1 : i],
                  r = fluid_h[i < n - 1 ? i + 1 : i];
            float sm = (l + fluid_h[i] * 2.0f + r) * 0.25f;
            surf[i] = base + vert_off - sm;
            if (surf[i] > inner_bot)
                surf[i] = inner_bot;
            ftop[i] = surf[i] -
                      foam_col[i] * (0.9f + 0.15f * sinf((float)i * 0.7f +
                                                         (float)frame * 0.04f));
        }

        if (level > 0.05f) {
            int spawn_chance =
                12 + (int)clampf((fabsf(ax_lag) + fabsf(ay_lag)) * 3.0f, 0.0f,
                                 55.0f);
            for (int k = 0; k < 2; k++) {
                if ((rand() % 100) >= spawn_chance)
                    continue;
                for (int i = 0; i < MAX_BUBBLES; i++) {
                    if (bubbles[i].active)
                        continue;
                    bubbles[i].active = true;
                    bubbles[i].x = (float)glass_left + 1.0f +
                                   site_frac[rand() % N_SITES] * (float)n;
                    bubbles[i].y = inner_bot - 0.3f;
                    bubbles[i].vy = 0.05f + (float)(rand() % 50) / 1000.0f;
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
            int ci = (int)(b->x - (float)glass_left - 1.0f);
            ci = ci < 0 ? 0 : ci >= n ? n - 1 : ci;
            float u_loc = 0.5f * (fluid_u[ci] + fluid_u[ci + 1]);

            b->x += u_loc * 0.12f * step_scale;
            b->x = clampf(b->x, (float)glass_left + 1.2f,
                          (float)glass_right - 1.2f);
            b->y -= b->vy * clampf(g_ratio, 0.25f, 2.2f) * step_scale;
            b->vy = fminf(b->vy * (1.0f + 0.012f * step_scale), 0.45f);
            b->phase += 0.15f * step_scale;

            if (b->y < surf[ci] + 0.3f) {
                b->active = false;
                foam_col[ci] += 0.012f;
                continue;
            }
            int bx = (int)(b->x + sinf(b->phase) * 0.35f), by = (int)b->y;
            if (bx > glass_left && bx < glass_right && by >= glass_top &&
                by < glass_bottom)
                bubmap[by][bx] = b->vy < 0.12f   ? "\xC2\xB7"
                                 : b->vy < 0.25f ? "\xC2\xB0"
                                                 : "o";
        }

        for (int i = 0; i < MAX_DROPS; i++) {
            Drop *d = &drops[i];
            if (!d->active)
                continue;
            d->vy += 0.12f * step_scale;
            d->x += d->vx * step_scale;
            d->y += d->vy * step_scale;
            if (d->x <= (float)glass_left + 1.0f) {
                d->x = (float)glass_left + 1.0f;
                d->vx = fabsf(d->vx) * 0.5f;
            } else if (d->x >= (float)glass_right - 1.0f) {
                d->x = (float)glass_right - 1.0f;
                d->vx = -fabsf(d->vx) * 0.5f;
            }
            int ci = (int)(d->x - (float)glass_left - 1.0f);
            ci = ci < 0 ? 0 : ci >= n ? n - 1 : ci;
            if (d->y >= surf[ci]) {
                d->active = false;
                fluid_h[ci] -= 0.06f;
                foam_col[ci] += 0.03f;
            } else if (d->y < 1.0f) {
                d->vy = fabsf(d->vy) * 0.3f;
            }
        }

        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++)
                set_cell(x, y, COL_DEF, COL_DEF, " ", cols, rows);

        Col hdr_fg = rgb(30, 30, 30), hdr_bg = rgb(235, 235, 235);
        for (int x = 0; x < cols; x++)
            set_cell(x, 0, hdr_fg, hdr_bg, " ", cols, rows);
        put_text(1, 0, hdr_fg, hdr_bg,
                 "[SPACE] Drink | [RMB+Drag] Move | [LMB+Drag] Shake | "
                 "[Arrows] Push | [R] Refill | [Q] Quit",
                 cols, rows);

        Col glass = rgb(210, 225, 235);
        for (int y = glass_top; y < glass_bottom; y++) {
            set_cell(glass_left, y, glass, COL_DEF, "\xE2\x94\x82", cols, rows);
            set_cell(glass_right, y, glass, COL_DEF, "\xE2\x94\x82", cols,
                     rows);
        }
        set_cell(glass_left, glass_bottom, glass, COL_DEF, "\xE2\x95\xB0", cols,
                 rows);
        set_cell(glass_right, glass_bottom, glass, COL_DEF, "\xE2\x95\xAF",
                 cols, rows);
        for (int x = glass_left + 1; x < glass_right; x++)
            set_cell(x, glass_bottom, glass, COL_DEF, "\xE2\x94\x80", cols,
                     rows);

        int h1 = glass_top + inner_h / 4, h2 = glass_bottom - inner_h / 4;
        if (h2 - h1 > 2) {
            set_cell(glass_right, h1, glass, COL_DEF, "\xE2\x94\x9C", cols,
                     rows);
            set_cell(glass_right, h2, glass, COL_DEF, "\xE2\x94\x9C", cols,
                     rows);
            set_cell(glass_right + 1, h1, glass, COL_DEF, "\xE2\x94\x80", cols,
                     rows);
            set_cell(glass_right + 1, h2, glass, COL_DEF, "\xE2\x94\x80", cols,
                     rows);
            set_cell(glass_right + 2, h1, glass, COL_DEF, "\xE2\x95\xAE", cols,
                     rows);
            set_cell(glass_right + 2, h2, glass, COL_DEF, "\xE2\x95\xAF", cols,
                     rows);
            for (int y = h1 + 1; y < h2; y++)
                set_cell(glass_right + 2, y, glass, COL_DEF, "\xE2\x94\x82",
                         cols, rows);
        }

        const Col foam_hi = {252, 250, 240}, foam_lo = {236, 214, 165};
        const Col beer_hi = {240, 165, 25}, beer_lo = {170, 90, 12};

        for (int i = 0; i < n; i++) {
            int x = glass_left + 1 + i;
            int glass_shade = (i == 0)       ? 28
                              : (i == 1)     ? 12
                              : (i == n - 1) ? -25
                                             : 0;
            float foam_th = surf[i] - ftop[i];

            for (int y = glass_top; y < glass_bottom; y++) {
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
                    lc[ln] = shade(lerp_col(foam_hi, foam_lo, fd), glass_shade);
                    lk[ln] = 1;
                    lw[ln++] = f;
                }
                if (b > 0.06f) {
                    float d = clampf((y0 + 0.5f - surf[i]) / (float)inner_h,
                                     0.0f, 1.0f);
                    lc[ln] = shade(lerp_col(beer_hi, beer_lo, d), glass_shade);
                    lk[ln] = 2;
                    lw[ln++] = b;
                }
                if (ln == 0)
                    continue;

                Col top = lc[0], bot = lc[ln - 1];
                float frac = lw[ln - 1];
                int lvl = ln == 1 ? 8 : (int)(frac * 8.0f + 0.5f);

                if (ln > 1 && lvl > 0 && lvl < 8) {
                    set_cell(x, y, bot, top, cava_blocks[lvl], cols, rows);
                } else {
                    bool use_bot = (ln == 1 || lvl >= 8);
                    Col solid = use_bot ? bot : top;
                    if (solid.r < 0)
                        continue;
                    bool is_foam = lk[use_bot ? ln - 1 : 0] == 1;
                    const char *gl = " ";
                    Col fg = shade(solid, -18);
                    if (is_foam) {
                        unsigned hv = hash2(x, y, (int)(frame / 30)) % 7;
                        gl = hv == 0 ? "\xC2\xB7" : hv == 1 ? "\xC2\xB0" : " ";
                    } else if (bubmap[y][x]) {
                        gl = bubmap[y][x];
                        fg = rgb(255, 250, 215);
                    }
                    set_cell(x, y, fg, solid, gl, cols, rows);
                }
            }
        }

        for (int i = 0; i < MAX_DROPS; i++) {
            if (!drops[i].active)
                continue;
            int dx = (int)drops[i].x;
            int dy = (int)drops[i].y;
            if (dy >= 1 && dy < glass_bottom && dx > glass_left &&
                dx < glass_right) {
                int ci = dx - glass_left - 1;
                if (ci >= 0 && ci < n && (float)dy < ftop[ci]) {
                    Col dc = drops[i].is_foam ? rgb(250, 245, 225)
                                              : rgb(240, 170, 30);
                    set_cell(dx, dy, dc, COL_DEF,
                             drops[i].is_foam ? "\xC2\xB0" : "\xE2\x80\xA2",
                             cols, rows);
                }
            }
        }

        if (pouring) {
            int sx = glass_left + 1 + n / 2;
            for (int k = 0; k < 2; k++) {
                int stop = (int)surf[n / 2 + k];
                for (int y = 1; y < stop && y < rows; y++)
                    set_cell(sx + k, y, rgb(255, 225, 130), rgb(235, 150, 20),
                             ((y + (int)frame) & 1) ? "\xE2\x96\x91" : " ",
                             cols, rows);
            }
        }

        if (level < 0.01f && !pouring) {
            const char *msg = "EMPTY! [R]";
            put_text(glass_left + n / 2 - 5, glass_top + inner_h / 2,
                     rgb(255, 90, 90), COL_DEF, msg, cols, rows);
        }

        char status[64];
        snprintf(status, sizeof(status), "Beer: %3d%%",
                 (int)(level * 100.0f + 0.5f));
        put_text(glass_left, rows - 1, rgb(200, 200, 200), COL_DEF, status,
                 cols, rows);

        flush_grid(cols, rows);
        sys_sleep_ms(14);
    }

    printf("\033[?1049l");
    sys_cleanup();
    return 0;
}
