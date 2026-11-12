#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
    #include "wininclude.h"
#else
    #include "posixinclude.h"
#endif

#define MAX_BUBBLES 64

typedef struct {
    float x;
    float y;
    float speed;
    char ch;
    bool active;
} Bubble;

static Bubble bubbles[MAX_BUBBLES];
static bool running = true;

void handle_sigint(int sig) {
    (void)sig;
    running = false;
}

// Эпилептическая тряска окна терминала при отрыжке
static void burp_window_shake(void) {
#ifdef _WIN32
    if (!hwnd) return;
    RECT r;
    GetWindowRect(hwnd, &r);
    for (int i = 0; i < 16; i++) {
        int ox = (rand() % 41) - 20;
        int oy = (rand() % 41) - 20;
        SetWindowPos(hwnd, NULL, r.left + ox, r.top + oy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        Sleep(15);
    }
    SetWindowPos(hwnd, NULL, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
#endif
}

int main(void) {
    signal(SIGINT, handle_sigint);
    sys_init();
    srand((unsigned int)time(NULL));

    // Параметры пива
    float beer_level = 0.82f;       // 1.0 = полное, 0.0 = пустое
    float tilt = 0.0f;             // Угол наклона поверхности
    float tilt_vel = 0.0f;         // Угловая скорость (инерция)
    float foam_thickness = 0.08f;  // Толщина слоя пены
    bool burped = false;

    // Предыдущие координаты окна
    int prev_wx = 0, prev_wy = 0;
    sys_get_window_pos(&prev_wx, &prev_wy);

    // Инициализация пузырьков
    for (int i = 0; i < MAX_BUBBLES; i++) {
        bubbles[i].active = false;
    }

    const char *foam_chars[] = {"#", "%", "*", "~", "@", "&"};

    while (running) {
        int cols = 80, rows = 24;
        sys_get_term_size(&cols, &rows);

        // 1. Физика перетаскивания окна мышкой
        int curr_wx = 0, curr_wy = 0;
        sys_get_window_pos(&curr_wx, &curr_wy);
        int dx = curr_wx - prev_wx;
        prev_wx = curr_wx;
        prev_wy = curr_wy;

        if (abs(dx) > 0) {
            // Дёргаем влево -> пиво качается вправо и наоборот
            tilt_vel -= (float)dx * 0.012f;
        }

        // 2. Управление с клавиатуры
        while (sys_kbhit()) {
            int ch = sys_getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) { // ESC или Q
                running = false;
            } else if (ch == ' ' || ch == 80) { // Пробел или Стрелка вниз = ПИТЬ
                if (beer_level > 0.02f) {
                    beer_level -= 0.025f;
                    tilt_vel += ((rand() % 10) - 5) * 0.005f; // Плещется при глотке
                }
            } else if (ch == 'r' || ch == 'R') { // Refill
                beer_level = 0.85f;
                burped = false;
            } else if (ch == 75) { // Стрелка влево
                tilt_vel -= 0.08f;
            } else if (ch == 77) { // Стрелка вправо
                tilt_vel += 0.08f;
            }
        }

        // 3. Пружинно-маятниковая физика поверхности (затухание)
        tilt_vel += -tilt * 0.16f; // Возврат в горизонт
        tilt_vel *= 0.91f;         // Трение
        tilt += tilt_vel;

        // Ограничение максимального угла
        if (tilt > 1.2f) tilt = 1.2f;
        if (tilt < -1.2f) tilt = -1.2f;

        // Отрыжка при пустом стакане
        if (beer_level <= 0.03f && !burped) {
            burp_window_shake();
            burped = true;
        }

        // 4. Генерация пузырьков
        if (beer_level > 0.05f && (rand() % 3 == 0)) {
            for (int i = 0; i < MAX_BUBBLES; i++) {
                if (!bubbles[i].active) {
                    bubbles[i].active = true;
                    bubbles[i].x = (float)(2 + (rand() % (cols - 4)));
                    bubbles[i].y = (float)(rows - 2);
                    bubbles[i].speed = 0.35f + ((rand() % 50) / 100.0f);
                    const char *bchars = "o.*°";
                    bubbles[i].ch = bchars[rand() % 4];
                    break;
                }
            }
        }

        // 5. РЕНДЕРИНГ КАДРА
        printf("\033[H"); // Курсор в левый верхний угол (без очистки экрана = без мерцания!)

        // Стакан занимает область внутри терминала
        int glass_left = 3;
        int glass_right = cols - 4;
        int glass_bottom = rows - 2;
        int glass_top = 2;
        int glass_height = glass_bottom - glass_top;

        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                // Границы стакана
                if (x == glass_left && y >= glass_top && y <= glass_bottom) {
                    printf("\033[1;37m|\033[0m");
                    continue;
                }
                if (x == glass_right && y >= glass_top && y <= glass_bottom) {
                    printf("\033[1;37m|\033[0m");
                    continue;
                }
                if (y == glass_bottom && x > glass_left && x < glass_right) {
                    printf("\033[1;37m=\033[0m");
                    continue;
                }

                // Вне стакана
                if (x < glass_left || x > glass_right || y < glass_top || y > glass_bottom) {
                    // Строка подсказок в самом верху
                    if (y == 0 && x == 2) {
                        const char *hint = "[SPACE] Drink  [<- / ->] Tilt  [R] Refill  [SHAKE WINDOW WITH MOUSE!]";
                        printf("\033[1;30;47m %s \033[0m", hint);
                        x += 69;
                    } else {
                        putchar(' ');
                    }
                    continue;
                }

                // Расчёт уровня поверхности с учётом наклона
                float norm_x = ((float)(x - glass_left) / (float)(glass_right - glass_left)) - 0.5f;
                float surface_y = (float)glass_bottom - (beer_level * (float)glass_height) + (norm_x * tilt * (float)glass_height);
                float foam_top_y = surface_y - (foam_thickness * (float)glass_height);

                if (beer_level <= 0.02f) {
                    // Пусто!
                    if (y == glass_top + glass_height / 2 && x == (glass_left + glass_right) / 2 - 8) {
                        printf("\033[1;31m* * * BURP! * * *\033[0m");
                        x += 16;
                    } else {
                        putchar(' ');
                    }
                } else if ((float)y < foam_top_y) {
                    // Воздух
                    putchar(' ');
                } else if ((float)y >= foam_top_y && (float)y <= surface_y) {
                    // Белая пена (TrueColor RGB)
                    const char *fc = foam_chars[rand() % 6];
                    printf("\033[38;2;240;240;240;48;2;255;255;255m%s\033[0m", fc);
                } else {
                    // Золотое пиво! Проверяем, есть ли тут пузырик
                    bool is_bubble = false;
                    for (int b = 0; b < MAX_BUBBLES; b++) {
                        if (bubbles[b].active && (int)bubbles[b].x == x && (int)bubbles[b].y == y) {
                            printf("\033[38;2;255;255;200;48;2;215;145;15m%c\033[0m", bubbles[b].ch);
                            is_bubble = true;
                            break;
                        }
                    }
                    if (!is_bubble) {
                        // Чистая янтарная жидкость
                        printf("\033[48;2;215;145;15m \033[0m");
                    }
                }
            }
            if (y < rows - 1) putchar('\n');
        }

        // Обновляем пузырьки
        for (int i = 0; i < MAX_BUBBLES; i++) {
            if (bubbles[i].active) {
                bubbles[i].y -= bubbles[i].speed;
                float norm_x = ((float)(bubbles[i].x - glass_left) / (float)(glass_right - glass_left)) - 0.5f;
                float surface_y = (float)glass_bottom - (beer_level * (float)glass_height) + (norm_x * tilt * (float)glass_height);
                if (bubbles[i].y <= surface_y) {
                    bubbles[i].active = false;
                }
            }
        }

        fflush(stdout);
        sys_sleep_ms(25); // ~40 FPS чистого кайфа
    }

    sys_cleanup();
    return 0;
}