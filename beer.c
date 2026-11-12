#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
    #include "wininclude.h"
#else
    #include "posixinclude.h"
    static inline void sys_get_cursor_pos(int *x, int *y) { *x = 0; *y = 0; }
    static inline bool sys_is_lbutton_down(void) { return false; }
    static inline bool sys_window_drag_update(void) { return false; }
#endif

#define MAX_COLS 300
#define MAX_ROWS 120
#define MAX_BUBBLES 160
#define N_SITES 9
#define BUFFER_SIZE (2 * 1024 * 1024)

// ---------------------------------------------------------------- цвет / сетка
typedef struct { short r, g, b; } Col;          // r < 0 => цвет терминала по умолчанию
typedef struct { Col fg, bg; char g[5]; } Cell;

static const Col COL_DEF = {-1, -1, -1};
static Cell grid[MAX_ROWS][MAX_COLS];
static const char *bubmap[MAX_ROWS][MAX_COLS];

// 1/8 ... 8/8 снизу вверх (CAVA-субпиксели)
static const char *cava_blocks[9] = {
    " ", "\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83", "\xE2\x96\x84",
    "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87", "\xE2\x96\x88"
};

static int clampi(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static Col rgb(int r, int g, int b) { Col c = {(short)clampi(r), (short)clampi(g), (short)clampi(b)}; return c; }
static Col lerp_col(Col a, Col b, float t) {
    return rgb((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t));
}
static Col shade(Col c, int d) { return c.r < 0 ? c : rgb(c.r + d, c.g + d, c.b + d); }
static bool col_eq(Col a, Col b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static void set_cell(int x, int y, Col fg, Col bg, const char *g, int cols, int rows) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) return;
    Cell *c = &grid[y][x];
    c->fg = fg; c->bg = bg;
    size_t n = strlen(g);
    if (n > 4) n = 4;
    memcpy(c->g, g, n);
    c->g[n] = 0;
}

static void put_text(int x, int y, Col fg, Col bg, const char *s, int cols, int rows) {
    while (*s && x < cols) {
        unsigned char ch = (unsigned char)*s;
        int len = ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4;
        char tmp[5] = {0};
        for (int k = 0; k < len && s[k]; k++) tmp[k] = s[k];
        set_cell(x++, y, fg, bg, tmp, cols, rows);
        s += len;
    }
}

static unsigned hash2(int x, int y, int t) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)t * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// ---------------------------------------------------------------- вывод кадра
static char frame_buf[BUFFER_SIZE];
static int buf_len = 0;

static void buf_write(const char *str) {
    while (*str && buf_len < BUFFER_SIZE - 1) frame_buf[buf_len++] = *str++;
}

// Цветовые escape-коды выдаём только при смене цвета => буфер в разы меньше, терминал не лагает
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
                if (c->fg.r < 0) strcpy(f, "39"); else snprintf(f, sizeof(f), "38;2;%d;%d;%d", c->fg.r, c->fg.g, c->fg.b);
                if (c->bg.r < 0) strcpy(b, "49"); else snprintf(b, sizeof(b), "48;2;%d;%d;%d", c->bg.r, c->bg.g, c->bg.b);
                snprintf(tmp, sizeof(tmp), "\033[%s;%sm", f, b);
                buf_write(tmp);
                lf = c->fg; lb = c->bg;
            }
            buf_write(c->g);
        }
    }
    buf_write("\033[0m");
    fwrite(frame_buf, 1, (size_t)buf_len, stdout);
    fflush(stdout);
}

// ---------------------------------------------------------------- физика
typedef struct { float x, y, vy, phase; bool active; } Bubble;
static Bubble bubbles[MAX_BUBBLES];
static volatile sig_atomic_t running = 1;

static void handle_sigint(int sig) { (void)sig; running = 0; }

static float wave_h[MAX_COLS], wave_v[MAX_COLS];

static void wave_step(int n, float limit) {
    const float tension = 0.004f, damp = 0.008f, spread = 0.20f;
    static float ld[MAX_COLS], rd[MAX_COLS];

    for (int i = 0; i < n; i++) {
        wave_v[i] += -tension * wave_h[i] - damp * wave_v[i];
        wave_h[i] += wave_v[i];
    }
    // распространение волн между соседями (дисперсия), симметрично => объём сохраняется
    for (int pass = 0; pass < 6; pass++) {
        for (int i = 0; i < n; i++) {
            ld[i] = i > 0     ? spread * (wave_h[i] - wave_h[i - 1]) : 0.0f;
            rd[i] = i < n - 1 ? spread * (wave_h[i] - wave_h[i + 1]) : 0.0f;
        }
        for (int i = 0; i < n; i++) {
            if (i > 0)     { wave_v[i - 1] += ld[i]; wave_h[i - 1] += ld[i]; }
            if (i < n - 1) { wave_v[i + 1] += rd[i]; wave_h[i + 1] += rd[i]; }
        }
    }
    // уровень жидкости в среднем не меняется
    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += wave_h[i];
    mean /= (float)n;
    for (int i = 0; i < n; i++) wave_h[i] = clampf(wave_h[i] - mean, -limit, limit);
}

// Толчок «бокала» вбок: жидкость отстаёт от стенки, к которой ускоряется бокал
static void tilt_impulse(int n, float accel) {
    float mid = (float)n * 0.5f;
    for (int i = 0; i < n; i++) wave_v[i] += -accel * 0.006f * ((float)i - mid) / mid;
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

    const float site_frac[N_SITES] = {0.07f, 0.17f, 0.31f, 0.43f, 0.52f, 0.64f, 0.75f, 0.86f, 0.94f};

    float level = 0.76f, target = 0.76f;   // уровень пива 0..1
    float foam_amt = 0.9f;                  // толщина пены в строках
    bool pouring = false;
    unsigned frame = 0;

    int prev_mx = 0, prev_my = 0;
    sys_get_cursor_pos(&prev_mx, &prev_my);
    float prev_drive = 0.0f, accel_s = 0.0f;
    int last_cols = 0, last_rows = 0;

    for (int i = 0; i < MAX_BUBBLES; i++) bubbles[i].active = false;

    while (running) {
        int cols = 80, rows = 24;
        sys_get_term_size(&cols, &rows);
        if (cols > MAX_COLS - 2) cols = MAX_COLS - 2;
        if (rows > MAX_ROWS - 1) rows = MAX_ROWS - 1;
        if (cols != last_cols || rows != last_rows) {
            printf("\033[2J");
            last_cols = cols; last_rows = rows;
        }

        int glass_left = 6, glass_right = cols - 7;
        int glass_bottom = rows - 2, glass_top = 3;
        int n = glass_right - glass_left - 1;       // внутренних колонок
        int inner_h = glass_bottom - glass_top;     // внутренних строк
        float inner_bot = (float)glass_bottom;

        if (n <= 8 || inner_h <= 4) { sys_sleep_ms(20); continue; }

        frame++;

        // --- 1. МЫШЬ: перетаскивание окна и инерция ---
        int cur_mx = 0, cur_my = 0;
        sys_get_cursor_pos(&cur_mx, &cur_my);
        int mouse_dx = cur_mx - prev_mx, mouse_dy = cur_my - prev_my;
        prev_mx = cur_mx; prev_my = cur_my;

        bool dragging = sys_window_drag_update();
        bool shaking = !dragging && sys_is_lbutton_down();
        float drive = (dragging || shaking) ? (float)mouse_dx : 0.0f;

        // ускорение окна (а не скорость!) раскачивает пиво
        accel_s = accel_s * 0.6f + (drive - prev_drive) * 0.4f;
        accel_s = clampf(accel_s, -40.0f, 40.0f);
        prev_drive = drive;
        if (fabsf(accel_s) > 0.05f) tilt_impulse(n, accel_s);

        float agitation = fabsf(accel_s) * 0.0006f + (dragging || shaking ? fabsf((float)mouse_dy) * 0.0004f : 0.0f);
        foam_amt += clampf(agitation, 0.0f, 0.03f);

        // --- 2. КЛАВИАТУРА ---
        while (sys_kbhit()) {
            int ch = sys_getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) running = 0;
            else if (ch == ' ' || ch == KEY_DOWN) {                 // глоток
                if (target > 0.0f) {
                    target -= 0.05f;
                    if (target < 0.0f) target = 0.0f;
                    wave_v[n / 2] -= 0.8f;
                    foam_amt -= 0.05f;
                }
            } else if (ch == 'r' || ch == 'R') { target = 0.80f; pouring = true; }
            else if (ch == KEY_LEFT)  tilt_impulse(n, 25.0f);
            else if (ch == KEY_RIGHT) tilt_impulse(n, -25.0f);
        }

        // --- 3. УРОВЕНЬ, НАЛИВАНИЕ, ПЕНА ---
        if (level < target) {
            level += 0.003f;
            if (pouring) {
                wave_v[n / 2] -= 0.12f;
                wave_v[n / 2 + 1] -= 0.08f;
                foam_amt += 0.008f;
            }
            if (level >= target) { level = target; pouring = false; }
        } else if (level > target) {
            level -= 0.0025f;
            if (level < target) level = target;
            pouring = false;
        }
        if (level < 0.02f && !pouring) foam_amt = 0.0f;
        if (foam_amt > 0.45f) foam_amt -= 0.0015f;
        foam_amt = clampf(foam_amt, 0.0f, fminf(4.0f, 0.2f * (float)inner_h));
        if (level < 0.02f) foam_amt = 0.0f;

        wave_step(n, (float)inner_h * 0.3f);

        // --- 4. ПОВЕРХНОСТЬ ПО КОЛОНКАМ ---
        static float surf[MAX_COLS], ftop[MAX_COLS];
        float base = inner_bot - level * (float)inner_h;
        for (int i = 0; i < n; i++) {
            float l = wave_h[i > 0 ? i - 1 : i], r = wave_h[i < n - 1 ? i + 1 : i];
            float sm = (l + wave_h[i] * 2.0f + r) * 0.25f;
            surf[i] = base - sm;
            ftop[i] = surf[i] - foam_amt * (0.9f + 0.2f * sinf((float)i * 0.7f + (float)frame * 0.02f));
        }

        // --- 5. ПУЗЫРИ: зарождаются в точках на дне, ускоряются, покачиваются ---
        if (level > 0.05f) {
            for (int k = 0; k < 2; k++) {
                if (rand() % 100 >= 12) continue;
                for (int i = 0; i < MAX_BUBBLES; i++) {
                    if (bubbles[i].active) continue;
                    bubbles[i].active = true;
                    bubbles[i].x = (float)glass_left + 1.0f + site_frac[rand() % N_SITES] * (float)n;
                    bubbles[i].y = inner_bot - 0.3f;
                    bubbles[i].vy = 0.05f + (float)(rand() % 50) / 1000.0f;
                    bubbles[i].phase = (float)(rand() % 628) / 100.0f;
                    break;
                }
            }
        }
        memset(bubmap, 0, sizeof(bubmap));
        for (int i = 0; i < MAX_BUBBLES; i++) {
            Bubble *b = &bubbles[i];
            if (!b->active) continue;
            b->y -= b->vy;
            b->vy = fminf(b->vy * 1.012f, 0.45f);
            b->phase += 0.15f;
            int ci = (int)(b->x - (float)glass_left - 1.0f);
            ci = ci < 0 ? 0 : ci >= n ? n - 1 : ci;
            if (b->y < surf[ci] + 0.3f) {
                b->active = false;
                foam_amt += 0.006f;     // пузырь лопнул — пены прибавилось
                continue;
            }
            int bx = (int)(b->x + sinf(b->phase) * 0.4f), by = (int)b->y;
            if (bx > glass_left && bx < glass_right && by >= glass_top && by < glass_bottom)
                bubmap[by][bx] = b->vy < 0.12f ? "\xC2\xB7" : b->vy < 0.25f ? "\xC2\xB0" : "o";
        }

        // --- 6. РЕНДЕР В СЕТКУ ---
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++) set_cell(x, y, COL_DEF, COL_DEF, " ", cols, rows);

        Col hdr_fg = rgb(30, 30, 30), hdr_bg = rgb(235, 235, 235);
        for (int x = 0; x < cols; x++) set_cell(x, 0, hdr_fg, hdr_bg, " ", cols, rows);
        put_text(1, 0, hdr_fg, hdr_bg,
                 "[ПРОБЕЛ] Пить | [ПКМ+мышь] Таскать окно | [ЛКМ+мышь] Шейкер | [R] Налить | [Q] Выход",
                 cols, rows);

        // стекло
        Col glass = rgb(210, 225, 235);
        for (int y = glass_top; y < glass_bottom; y++) {
            set_cell(glass_left, y, glass, COL_DEF, "\xE2\x94\x82", cols, rows);
            set_cell(glass_right, y, glass, COL_DEF, "\xE2\x94\x82", cols, rows);
        }
        set_cell(glass_left, glass_bottom, glass, COL_DEF, "\xE2\x95\xB0", cols, rows);
        set_cell(glass_right, glass_bottom, glass, COL_DEF, "\xE2\x95\xAF", cols, rows);
        for (int x = glass_left + 1; x < glass_right; x++)
            set_cell(x, glass_bottom, glass, COL_DEF, "\xE2\x94\x80", cols, rows);

        // ручка кружки
        int h1 = glass_top + inner_h / 4, h2 = glass_bottom - inner_h / 4;
        if (h2 - h1 > 2) {
            set_cell(glass_right, h1, glass, COL_DEF, "\xE2\x94\x9C", cols, rows);
            set_cell(glass_right, h2, glass, COL_DEF, "\xE2\x94\x9C", cols, rows);
            set_cell(glass_right + 1, h1, glass, COL_DEF, "\xE2\x94\x80", cols, rows);
            set_cell(glass_right + 1, h2, glass, COL_DEF, "\xE2\x94\x80", cols, rows);
            set_cell(glass_right + 2, h1, glass, COL_DEF, "\xE2\x95\xAE", cols, rows);
            set_cell(glass_right + 2, h2, glass, COL_DEF, "\xE2\x95\xAF", cols, rows);
            for (int y = h1 + 1; y < h2; y++)
                set_cell(glass_right + 2, y, glass, COL_DEF, "\xE2\x94\x82", cols, rows);
        }

        const Col foam_hi = {252, 250, 240}, foam_lo = {236, 214, 165};
        const Col beer_hi = {240, 165, 25},  beer_lo = {170, 90, 12};

        for (int i = 0; i < n; i++) {
            int x = glass_left + 1 + i;
            int glass_shade = (i == 0) ? 28 : (i == 1) ? 12 : (i == n - 1) ? -25 : 0;   // блик слева, тень справа
            float foam_th = surf[i] - ftop[i];

            for (int y = glass_top; y < glass_bottom; y++) {
                float y0 = (float)y, y1 = (float)y + 1.0f;
                float f = ov(ftop[i], surf[i], y0, y1);
                float b = ov(surf[i], inner_bot, y0, y1);
                float a = 1.0f - f - b;
                if (a < 0.0f) a = 0.0f;

                Col lc[3]; float lw[3]; int lk[3]; int ln = 0;
                if (a > 0.06f) { lc[ln] = COL_DEF; lk[ln] = 0; lw[ln++] = a; }
                if (f > 0.06f) {
                    float fd = foam_th > 0.1f ? clampf((y0 + 0.5f - ftop[i]) / foam_th, 0.0f, 1.0f) : 0.0f;
                    lc[ln] = shade(lerp_col(foam_hi, foam_lo, fd), glass_shade); lk[ln] = 1; lw[ln++] = f;
                }
                if (b > 0.06f) {
                    float d = clampf((y0 + 0.5f - surf[i]) / (float)inner_h, 0.0f, 1.0f);
                    lc[ln] = shade(lerp_col(beer_hi, beer_lo, d), glass_shade); lk[ln] = 2; lw[ln++] = b;
                }
                if (ln == 0) continue;

                Col top = lc[0], bot = lc[ln - 1];
                float frac = lw[ln - 1];
                int lvl = ln == 1 ? 8 : (int)(frac * 8.0f + 0.5f);

                if (ln > 1 && lvl > 0 && lvl < 8) {
                    // граница слоёв — CAVA-блок: снизу нижний слой, фон — верхний
                    set_cell(x, y, bot, top, cava_blocks[lvl], cols, rows);
                } else {
                    bool use_bot = (ln == 1 || lvl >= 8);
                    Col solid = use_bot ? bot : top;
                    if (solid.r < 0) continue;
                    bool is_foam = lk[use_bot ? ln - 1 : 0] == 1;
                    const char *gl = " ";
                    Col fg = shade(solid, -18);
                    if (is_foam) {
                        unsigned hv = hash2(x, y, (int)(frame / 50)) % 7;
                        gl = hv == 0 ? "\xC2\xB7" : hv == 1 ? "\xC2\xB0" : " ";
                    } else if (bubmap[y][x]) {
                        gl = bubmap[y][x];
                        fg = rgb(255, 250, 215);
                    }
                    set_cell(x, y, fg, solid, gl, cols, rows);
                }
            }
        }

        // струя при наливании
        if (pouring) {
            int sx = glass_left + 1 + n / 2;
            for (int k = 0; k < 2; k++) {
                int stop = (int)surf[n / 2 + k];
                for (int y = 1; y < stop && y < rows; y++)
                    set_cell(sx + k, y, rgb(255, 225, 130), rgb(235, 150, 20),
                             ((y + (int)frame) & 1) ? "\xE2\x96\x91" : " ", cols, rows);
            }
        }

        if (level < 0.01f && !pouring) {
            const char *msg = "ПУСТО! [R]";
            put_text(glass_left + n / 2 - 4, glass_top + inner_h / 2, rgb(255, 90, 90), COL_DEF, msg, cols, rows);
        }

        char status[64];
        snprintf(status, sizeof(status), "Пива: %3d%%", (int)(level * 100.0f + 0.5f));
        put_text(glass_left, rows - 1, rgb(200, 200, 200), COL_DEF, status, cols, rows);

        flush_grid(cols, rows);
        sys_sleep_ms(14);
    }

    printf("\033[?1049l");
    sys_cleanup();
    return 0;
}