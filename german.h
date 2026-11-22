#ifndef GERMAN_H
#define GERMAN_H

#include "drink.h"

static const Drink DRINK_GERMAN = {.name = "German Beer",
                                   .beer_hi = {244, 178, 36},
                                   .beer_lo = {190, 108, 16},
                                   .foam_hi = {255, 255, 252},
                                   .foam_lo = {240, 230, 198},
                                   .bubble_col = {255, 250, 210},
                                   .drop_col = {245, 180, 38},
                                   .foam_drop_col = {255, 255, 250},

                                   .g_base = 0.88f,
                                   .drag = 0.026f,
                                   .visc = 0.06f,
                                   .accel_gain = 1.0f,

                                   .foam_init = 1.5f,
                                   .foam_base = 0.85f,
                                   .foam_max = 6.2f,
                                   .foam_agitate = 0.0048f,
                                   .foam_decay = 0.0011f,
                                   .foam_advect = 0.026f,

                                   .drop_thresh = 0.24f,
                                   .drop_chance = 32,
                                   .bubble_rate = 20,
                                   .bubble_speed = 0.95f,
                                   .fine_bubbles = false};

#endif
