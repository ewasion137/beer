#ifndef DRINK_H
#define DRINK_H

#include <stdbool.h>

typedef struct {
    short r, g, b;
} Col;

typedef struct {
    const char *name;
    Col beer_hi;
    Col beer_lo;
    Col foam_hi;
    Col foam_lo;
    Col bubble_col;
    Col drop_col;
    Col foam_drop_col;

    float g_base;
    float drag;
    float visc;
    float accel_gain;

    float foam_init;
    float foam_base;
    float foam_max;
    float foam_agitate;
    float foam_decay;
    float foam_advect;

    float drop_thresh;
    int drop_chance;
    int bubble_rate;
    float bubble_speed;
    bool fine_bubbles;
} Drink;

#endif
