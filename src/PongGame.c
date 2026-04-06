#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/timer.h"
#include "esp_timer.h"
#include "esp_system.h"  // esp_random()
uint32_t esp_random(void);

// ================= MATRIZ =================
#define ROWS 8
#define COLS 8
static const gpio_num_t ROW_PINS[8] = {13, 16, 17, 18, 19, 21, 22, 23};
static const gpio_num_t COL_PINS[8] = {25, 26, 27, 32, 33, 14, 5, 4};
#define ROW_ON 0
#define ROW_OFF 1
#define COL_ON 1
#define COL_OFF 0
#define BTN_J1_UP 34
#define BTN_J1_DOWN 35
#define BTN_J2_UP 36
#define BTN_J2_DOWN 39
#define BALL_SPEED_START 10
#define BALL_SPEED_MIN 3
#define TASK_TICKS_MS 20
#define MENU_TIME_US 4000000
#define MENU_STEP_US 500000
#define HOLD_TIME_US 2000000  
#define CPU_REACT_EASY 4
#define CPU_REACT_NORMAL 3
#define CPU_REACT_HARD 2
#define SERVE_TIME_US 400000
#define SERVE_BLINK_US 80000
#define SPEEDUP_EVERY_HITS 10
#define BTN_DEBOUNCE_US 200000

// ================= ESTADOS =================
typedef enum { MENU, JUGANDO } estado_juego_t;
typedef enum { VS_J2, VS_CPU } modo_juego_t;

// ================= VARIABLES =================
static volatile uint8_t frame[ROWS];
static volatile int current_row = 0;
static volatile estado_juego_t estado = MENU;
static volatile modo_juego_t modo = VS_CPU;
static volatile int p1 = 3;
static volatile int p2 = 3;
static volatile int ball_r = 3;
static volatile int ball_c = 3;
static volatile int dr = 1;
static volatile int dc = -1;
static int ball_counter = 0;
static int ball_speed = BALL_SPEED_START;
static int64_t menu_start_us = 0;
static int64_t btn_press_time = 0;
static bool btn_holding = false;
static int cpu_tick = 0;
static int cpu_react = CPU_REACT_NORMAL;
static int cpu_error = 0;
static int cpu_error_ticks = 0;
static volatile bool serving = false;
static int64_t serve_start_us = 0;
static volatile int last_loser = 0;
static int paddle_hit_count = 0;
static volatile int64_t last_j1_up = 0;
static volatile int64_t last_j1_down = 0;
static volatile int64_t last_j2_up = 0;
static volatile int64_t last_j2_down = 0;

// ================= MUX ISR =================
bool IRAM_ATTR mux_callback(void *arg) {

    gpio_set_level(ROW_PINS[current_row], ROW_OFF);

    for (int c = 0; c < COLS; c++) {
        gpio_set_level(COL_PINS[c], COL_OFF);
    }

    current_row = (current_row + 1) % ROWS;

    uint8_t data = frame[current_row];
    for (int c = 0; c < COLS; c++) {
        int bit = (data >> (7 - c)) & 1;
        gpio_set_level(COL_PINS[c], bit ? COL_ON : COL_OFF);
    }

    gpio_set_level(ROW_PINS[current_row], ROW_ON);

    return true;
}

// ================= UTILIDADES =================
static inline void clear_frame(void) {
    memset((void*)frame, 0, sizeof(frame));
}

static inline bool any_button_pressed(void) {
    return !gpio_get_level(BTN_J1_UP)   ||
           !gpio_get_level(BTN_J1_DOWN) ||
           !gpio_get_level(BTN_J2_UP)   ||
           !gpio_get_level(BTN_J2_DOWN);
}

static inline void clamp_ball(void) {
    if (ball_r < 0) ball_r = 0;
    if (ball_r > 7) ball_r = 7;
    if (ball_c < 0) ball_c = 0;
    if (ball_c > 7) ball_c = 7;
}

static inline void reset_round(int loser) {
    last_loser = loser;

    ball_r = 3;
    ball_c = 3;

    if (loser == 1) dc = -1;
    else if (loser == 2) dc = +1;
    else dc = (esp_random() & 1) ? +1 : -1;

    dr = (int)(esp_random() % 3) - 1;
    if (dr == 0) dr = 1;

    ball_speed = BALL_SPEED_START;
    ball_counter = 0;

    serving = true;
    serve_start_us = esp_timer_get_time();

    paddle_hit_count = 0;
}

// ================= ISRs BOTONES =================
void IRAM_ATTR isr_j1_up(void* arg) {
    int64_t now = esp_timer_get_time();
    if (now - last_j1_up < BTN_DEBOUNCE_US) return;
    last_j1_up = now;

    if (estado == JUGANDO && p1 > 0) p1--;
}

void IRAM_ATTR isr_j1_down(void* arg) {

    int64_t now = esp_timer_get_time();
    if (now - last_j1_down < BTN_DEBOUNCE_US) return;
    last_j1_down = now;

    if (estado == JUGANDO && p1 < 5) p1++;
}

void IRAM_ATTR isr_j2_up(void* arg) {
    int64_t now = esp_timer_get_time();
    if (now - last_j2_up < BTN_DEBOUNCE_US) return;
    last_j2_up = now;

    if (estado == MENU) {
        modo = VS_J2;
        estado = JUGANDO;
        reset_round(0);
        return;
    }

    if (estado == JUGANDO && modo == VS_J2 && p2 > 0) p2--;
}

void IRAM_ATTR isr_j2_down(void* arg) {
    int64_t now = esp_timer_get_time();
    if (now - last_j2_down < BTN_DEBOUNCE_US) return;
    last_j2_down = now;

    if (estado == MENU) {
        modo = VS_J2;
        estado = JUGANDO;
        reset_round(0);
        return;
    }

    if (estado == JUGANDO && modo == VS_J2 && p2 < 5) p2++;
}

// ================= DIBUJO =================
static inline void draw_paddles_and_ball(void) {
    clear_frame();

    bool blink_on = true;
    if (serving) {
        int64_t now = esp_timer_get_time();
        blink_on = (((now - serve_start_us) / SERVE_BLINK_US) % 2) == 0;
    }

    // Paleta J1 (col 0) parpadea si J1 perdió
    if (!(serving && last_loser == 1 && !blink_on)) {
        for (int i = 0; i < 3; i++) {
            frame[p1 + i] |= (1 << (7 - 0));
        }
    }

    // Paleta J2/CPU (col 7) parpadea si J2/CPU perdió
    if (!(serving && last_loser == 2 && !blink_on)) {
        for (int i = 0; i < 3; i++) {
            frame[p2 + i] |= (1 << (7 - 7));
        }
    }

    // Pelota (parpadea durante serving)
    if (!serving || blink_on) {
        frame[ball_r] |= (1 << (7 - ball_c));
    }
}

static inline void draw_menu(void) {
    clear_frame();

    // Paleta J1 fija (col 0)
    for (int i = 0; i < 3; i++) {
        frame[p1 + i] |= (1 << (7 - 0));
    }

    // Contador en columna derecha (col 7): barra de abajo -> arriba
    int64_t now = esp_timer_get_time();
    int step = (int)((now - menu_start_us) / MENU_STEP_US); // 0..7
    if (step < 0) step = 0;
    if (step > 7) step = 7;

    for (int k = 0; k <= step; k++) {
        int r = 7 - k;
        frame[r] |= (1 << (7 - 7));
    }

    // punto al centro
    frame[3] |= (1 << (7 - 3));
}

// ================= FÍSICA =================

static inline void maybe_speedup(void) {
    paddle_hit_count++;

    int hits_needed = (ball_speed <= 4) ? 8 : 5;

    if (paddle_hit_count >= hits_needed) {
        paddle_hit_count = 0;
        if (ball_speed > BALL_SPEED_MIN) ball_speed--;
    }
}

static inline void apply_paddle_bounce(int hit_offset) {
    maybe_speedup();
    if (hit_offset == 0) dr = -1;
    else if (hit_offset == 1) dr = 0;
    else dr = +1;

    paddle_hit_count++;
    if (paddle_hit_count >= SPEEDUP_EVERY_HITS) {
        paddle_hit_count = 0;
        if (ball_speed > BALL_SPEED_MIN) ball_speed--;
    }
}


void move_ball(void) {
    int next_r = ball_r + dr;
    int next_c = ball_c + dc;

    // rebote paredes (arriba/abajo)
    if (next_r <= 0 || next_r >= 7) {
        dr *= -1;
        next_r = ball_r + dr;
    }

    // paleta izquierda (col 0) cuando va hacia la izquierda
    if (dc < 0 && next_c == 0) {
        if (next_r >= p1 && next_r < p1 + 3) {
            int offset = next_r - p1; // 0..2
            dc = +1;
            apply_paddle_bounce(offset);
            next_c = ball_c + dc;
        } else {
            // J1 falló -> serve hacia J1 (perdió)
            reset_round(1);
            return;
        }
    }

    // paleta derecha (col 7) cuando va hacia la derecha
    if (dc > 0 && next_c == 7) {
        if (next_r >= p2 && next_r < p2 + 3) {
            int offset = next_r - p2; // 0..2
            dc = -1;
            apply_paddle_bounce(offset);
            next_c = ball_c + dc;
        } else {
            // J2/CPU falló -> serve hacia derecha (perdió)
            reset_round(2);
            return;
        }
    }

    ball_r = next_r;
    ball_c = next_c;
    clamp_ball();
}

// ================= CPU =================
static inline int target_center_of_paddle(int top) {
    return top + 1; 
}

void cpu_update(void) {
    if (modo != VS_CPU) return;

    cpu_tick++;
    if (cpu_tick % cpu_react != 0) return;

    int target_r;

    if (dc > 0) {
        // pelota viene hacia CPU
        cpu_error_ticks++;
        if (cpu_error_ticks >= 8) {
            cpu_error_ticks = 0;
            cpu_error = (int)(esp_random() % 3) - 1; // -1,0,1
        }
        target_r = ball_r + cpu_error;
    } else {
        // pelota va hacia el otro lado -> vuelve al centro
        target_r = 3;
    }

    int center = target_center_of_paddle(p2);

    if (target_r > center) {
        if (p2 < 5) p2++;
    } else if (target_r < center) {
        if (p2 > 0) p2--;
    }
}

// ================= TASK PRINCIPAL =================
void game_task(void* arg) {
    menu_start_us = esp_timer_get_time();
    estado = MENU;
    modo = VS_CPU;

    reset_round(0);

    while (1) {

        // reset por hold 2s
        if (any_button_pressed()) {
            if (!btn_holding) {
                btn_holding = true;
                btn_press_time = esp_timer_get_time();
            } else if (esp_timer_get_time() - btn_press_time > HOLD_TIME_US) {
                estado = MENU;
                modo = VS_CPU;
                menu_start_us = esp_timer_get_time();

                p1 = 3;
                p2 = 3;
                reset_round(0); // inicio, sin perdedor

                btn_holding = false;
            }
        } else {
            btn_holding = false;
        }

        if (estado == MENU) {

            int64_t now = esp_timer_get_time();
            if (now - menu_start_us >= MENU_TIME_US) {
                modo = VS_CPU;
                estado = JUGANDO;
                reset_round(0);
            }

            draw_menu();

        } else { // JUGANDO

            if (modo == VS_CPU) {
                cpu_update();
            }

            if (serving) {
                int64_t now = esp_timer_get_time();
                if (now - serve_start_us >= SERVE_TIME_US) {
                    serving = false;
                    ball_counter = 0;
                }
            } else {
                ball_counter++;
                if (ball_counter >= ball_speed) {
                    move_ball();
                    ball_counter = 0;
                }
            }

            draw_paddles_and_ball();
        }

        vTaskDelay(pdMS_TO_TICKS(TASK_TICKS_MS));
    }
}

// ================= GPIO =================
void setup_gpio(void) {
    // filas
    uint64_t mask_rows = 0;
    for (int i = 0; i < ROWS; i++) mask_rows |= (1ULL << ROW_PINS[i]);

    gpio_config_t out_rows = {
        .pin_bit_mask = mask_rows,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&out_rows);

    for (int i = 0; i < ROWS; i++) gpio_set_level(ROW_PINS[i], ROW_OFF);

    // columnas
    uint64_t mask_cols = 0;
    for (int i = 0; i < COLS; i++) mask_cols |= (1ULL << COL_PINS[i]);

    gpio_config_t out_cols = {
        .pin_bit_mask = mask_cols,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&out_cols);

    for (int i = 0; i < COLS; i++) gpio_set_level(COL_PINS[i], COL_OFF);

    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << BTN_J1_UP) | (1ULL << BTN_J1_DOWN) |
                        (1ULL << BTN_J2_UP) | (1ULL << BTN_J2_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&in_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_J1_UP, isr_j1_up, NULL);
    gpio_isr_handler_add(BTN_J1_DOWN, isr_j1_down, NULL);
    gpio_isr_handler_add(BTN_J2_UP, isr_j2_up, NULL);
    gpio_isr_handler_add(BTN_J2_DOWN, isr_j2_down, NULL);
}

// ================= MAIN =================
void app_main(void) {
    setup_gpio();

    timer_config_t timer_conf = {
        .divider = 80,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = true
    };

    timer_init(TIMER_GROUP_0, TIMER_0, &timer_conf);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 2000);
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, mux_callback, NULL, 0);
    timer_start(TIMER_GROUP_0, TIMER_0);

    xTaskCreate(game_task, "game", 4096, NULL, 5, NULL);
}