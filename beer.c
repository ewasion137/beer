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
    // Заглушки под POSIX для мыши
    static inline void sys_get_cursor_pos(int *x, int *y) { *x = 0; *y = 0; }
    static inline bool sys_is_lbutton_down(void) { return false; }
#endif

#define MAX_COLS 300
#define MAX_BUBBLES 80
#define BUFFER_SIZE (512 * 1024) // 512 КБ кадровый буфер (0% мерцания)

typedef struct {
    float x;
    float y;
    float speed;
    char ch;
    bool active;
} Bubble;

static Bubble bubbles[MAX_BUBBLES];
static bool running = true;
static char frame_buf[BUFFER_SIZE];
static int buf_len = 0;

static inline void buf_write(const char *str) {
    while (*str && buf_len < BUFFER_SIZE - 1) {
        frame_buf[buf_len++] = *str++;
    }
}

void handle_sigint(int sig) {
    (void)sig;
    running = false;
}

int main(void) {
    signal(SIGINT, handle_sigint);
    sys_init();
    srand((unsigned int)time(NULL));

    // Параметры бокала и жидкости
    float beer_mass = 0.78f;   // Уровень наполнения
    float wave_height[MAX_COLS];
    float wave_vel[MAX_COLS];
    for (int i = 0; i < MAX_COLS; i++) {
        wave_height[i] = 0.0f;
        wave_vel[i] = 0.0f;
    }

    int prev_wx = 0, prev_wy = 0;
    int prev_mx = 0, prev_my = 0;
    sys_get_window_pos(&prev_wx, &prev_wy);
    sys_get_cursor_pos(&prev_mx, &prev_my);

    for (int i = 0; i < MAX_BUBBLES; i++) bubbles[i].active = false;

    while (running) {
        int cols = 80, rows = 24;
        sys_get_term_size(&cols, &rows);
        if (cols > MAX_COLS - 2) cols = MAX_COLS - 2;

        // Геометрия бокала
        int glass_left = 6;
        int glass_right = cols - 7;
        int glass_bottom = rows - 2;
        int glass_top = 3;
        int glass_width = glass_right - glass_left - 1;
        int glass_height = glass_bottom - glass_top;

        if (glass_width <= 4 || glass_height <= 4) {
            sys_sleep_ms(30);
            continue;
        }

        // --- 1. ТРЕКИНГ ДВИЖЕНИЯ (Окно или Зажатая ЛКМ) ---
        int cur_wx = 0, cur_wy = 0;
        int cur_mx = 0, cur_my = 0;
        sys_get_window_pos(&cur_wx, &cur_wy);
        sys_get_cursor_pos(&cur_mx, &cur_my);

        int delta_x = (cur_wx - prev_wx);
        int mouse_dx = (cur_mx - prev_mx);
        prev_wx = cur_wx;
        prev_wy = cur_wy;
        prev_mx = cur_mx;
        prev_my = cur_my;

        // Если зажата ЛКМ внутри окна — шейкер работает от любого движения мыши (без фризов винды!)
        float impulse = 0.0f;
        if (sys_is_lbutton_down() && abs(mouse_dx) > 1) {
            impulse -= (float)mouse_dx * 0.08f;
        } else if (abs(delta_x) > 0) {
            impulse -= (float)delta_x * 0.05f;
        }

        // Толкаем волну на границах при ускорении
        if (fabsf(impulse) > 0.01f) {
            wave_vel[1] += impulse * 1.5f;
            wave_vel[glass_width - 2] -= impulse * 1.5f;
        }

        // --- 2. КЛАВИАТУРА ---
        while (sys_kbhit()) {
            int ch = sys_getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) running = false;
            else if (ch == ' ' || ch == 80) { // Пробел / Вниз = ПИТЬ
                if (beer_mass > 0.01f) {
                    beer_mass -= 0.03f;
                    wave_vel[glass_width / 2] -= 0.8f; // Всплеск от глотка
                }
            } else if (ch == 'r' || ch == 'R') {
                beer_mass = 0.80f;
            } else if (ch == 75) { // Влево
                wave_vel[1] += 1.2f;
            } else if (ch == 77) { // Вправо
                wave_vel[glass_width - 2] -= 1.2f;
            }
        }

        // --- 3. ФИЗИКА ВОЛН (SHALLOW WATER / ПРУЖИННАЯ СЕТКА) ---
        float k = 0.12f;       // Жесткость связи между колонками жидкости
        float damping = 0.94f; // Затухание волны
        
        // Интегрирование скоростей
        for (int i = 1; i < glass_width; i++) {
            float left_h  = (i > 1) ? wave_height[i - 1] : wave_height[i];
            float right_h = (i < glass_width - 1) ? wave_height[i + 1] : wave_height[i];
            
            // Сила возврата в 0 + передача энергии соседям
            float force = -0.08f * wave_height[i] + k * (left_h + right_h - 2.0f * wave_height[i]);
            wave_vel[i] = (wave_vel[i] + force) * damping;
        }

        for (int i = 1; i < glass_width; i++) {
            wave_height[i] += wave_vel[i];
            // Ограничение всплеска, чтобы не выбивало за стакан
            if (wave_height[i] > glass_height * 0.7f) wave_height[i] = glass_height * 0.7f;
            if (wave_height[i] < -glass_height * 0.7f) wave_height[i] = -glass_height * 0.7f;
        }

        // --- 4. ПУЗЫРИ ---
        if (beer_mass > 0.05f && (rand() % 2 == 0)) {
            for (int i = 0; i < MAX_BUBBLES; i++) {
                if (!bubbles[i].active) {
                    bubbles[i].active = true;
                    bubbles[i].x = (float)(glass_left + 2 + (rand() % (glass_width - 3)));
                    bubbles[i].y = (float)(glass_bottom - 1);
                    bubbles[i].speed = 0.25f + ((rand() % 40) / 100.0f);
                    const char *b_types = "o.°·";
                    bubbles[i].ch = b_types[rand() % 4];
                    break;
                }
            }
        }

        // --- 5. РЕНДЕР КАДРА В ЕДИНЫЙ ПАМЯТНЫЙ БУФЕР (0 МЕРЦАНИЯ) ---
        buf_len = 0;
        buf_write("\033[H"); // Домой

        // Хедер
        buf_write("\033[1;30;47m [SPACE] Пить | [R] Налить | Зажми ЛКМ и тряси мышь | [Q] Выход \033[0m\n");

        for (int y = 1; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                // Границы бокала (красивый UTF-8)
                if (y == glass_top && x == glass_left) { buf_write("\033[1;37m╭\033[0m"); continue; }
                if (y == glass_top && x == glass_right) { buf_write("\033[1;37m╮\033[0m"); continue; }
                if (y == glass_bottom && x == glass_left) { buf_write("\033[1;37m╰\033[0m"); continue; }
                if (y == glass_bottom && x == glass_right) { buf_write("\033[1;37m╯\033[0m"); continue; }
                if (y == glass_bottom && x > glass_left && x < glass_right) { buf_write("\033[1;37m─\033[0m"); continue; }
                if ((x == glass_left || x == glass_right) && y > glass_top && y < glass_bottom) {
                    buf_write("\033[1;37m│\033[0m");
                    continue;
                }

                // Внутри стакана
                if (x > glass_left && x < glass_right && y > glass_top && y < glass_bottom) {
                    int col_idx = x - glass_left;
                    float base_surface = (float)glass_bottom - (beer_mass * (float)glass_height);
                    float surf_y = base_surface - wave_height[col_idx];
                    float foam_y = surf_y - 1.4f; // Высота пены

                    if (beer_mass <= 0.01f) {
                        if (y == glass_top + glass_height / 2 && x == glass_left + glass_width / 2 - 4) {
                            buf_write("\033[1;31mПУСТО! [R]\033[0m");
                            x += 9;
                        } else {
                            buf_write(" ");
                        }
                    } else if ((float)y < foam_y) {
                        buf_write(" "); // Воздух
                    } else if ((float)y >= foam_y && (float)y <= surf_y) {
                        // Пышная белая пена
                        const char *fchars[] = {"#", "%", "*", "O", "o", "~"};
                        char fbuf[48];
                        snprintf(fbuf, sizeof(fbuf), "\033[38;2;255;255;255;48;2;240;235;220m%s\033[0m", fchars[(x + y + rand()) % 6]);
                        buf_write(fbuf);
                    } else {
                        // Пиво: проверяем пузырьки
                        bool is_bub = false;
                        for (int b = 0; b < MAX_BUBBLES; b++) {
                            if (bubbles[b].active && (int)bubbles[b].x == x && (int)bubbles[b].y == y) {
                                char bbuf[48];
                                snprintf(bbuf, sizeof(bbuf), "\033[38;2;255;255;180;48;2;210;140;15m%c\033[0m", bubbles[b].ch);
                                buf_write(bbuf);
                                is_bub = true;
                                break;
                            }
                        }
                        if (!is_bub) {
                            // Янтарный градиент (глубже — темнее)
                            int depth = y - (int)surf_y;
                            int r = 230 - depth * 3; if (r < 180) r = 180;
                            int g = 150 - depth * 4; if (g < 95) g = 95;
                            char pbuf[48];
                            snprintf(pbuf, sizeof(pbuf), "\033[48;2;%d;%d;10m \033[0m", r, g);
                            buf_write(pbuf);
                        }
                    }
                    continue;
                }

                // Всё, что снаружи стакана
                buf_write(" ");
            }
            if (y < rows - 1) buf_write("\n");
        }

        // Обновление пузырьков
        for (int i = 0; i < MAX_BUBBLES; i++) {
            if (bubbles[i].active) {
                bubbles[i].y -= bubbles[i].speed;
                int c_idx = (int)bubbles[i].x - glass_left;
                if (c_idx < 1) c_idx = 1;
                if (c_idx >= glass_width) c_idx = glass_width - 1;
                float surf = ((float)glass_bottom - (beer_mass * (float)glass_height)) - wave_height[c_idx];
                if (bubbles[i].y <= surf) bubbles[i].active = false;
            }
        }

        // ВЫПЛЁСКИВАЕМ ВЕСЬ КАДР В КОНСОЛЬ ЗА ОДИН РАЗ
        fwrite(frame_buf, 1, buf_len, stdout);
        fflush(stdout);

        sys_sleep_ms(16); // Ровные ~60-70 FPS под 144Hz мониторы
    }

    sys_cleanup();
    return 0;
}