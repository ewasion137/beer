#ifndef IRISH_H
#define IRISH_H

#include "drink.h"

static const Drink DRINK_IRISH = {
    .name = "Irish Beer",
    .beer_hi = {42, 20, 10},
    .beer_lo = {10, 5, 3},
    .foam_hi = {248, 240, 222},
    .foam_lo = {216, 192, 152},
    .bubble_col = {196, 162, 118},
    .drop_col = {68, 32, 14},
    .foam_drop_col = {246, 238, 218},

    .g_base = 0.70f,
    .drag = 0.048f,
    .visc = 0.14f,
    .accel_gain = 0.85f,

    .foam_init = 1.8f,
    .foam_base = 1.35f,
    .foam_max = 5.2f,
    .foam_agitate = 0.0020f,
    .foam_decay = 0.0005f,
    .foam_advect = 0.014f,

    .drop_thresh = 0.34f,
    .drop_chance = 16,
    .bubble_rate = 30,
    .bubble_speed = 0.45f,
    .fine_bubbles = true
};

#endif
