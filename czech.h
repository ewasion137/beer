#ifndef CZECH_H
#define CZECH_H

#include "drink.h"

static const Drink DRINK_CZECH = {.name = "Czech Beer",
                                  .beer_hi = {252, 206, 32},
                                  .beer_lo = {212, 140, 12},
                                  .foam_hi = {255, 253, 244},
                                  .foam_lo = {242, 228, 188},
                                  .bubble_col = {255, 255, 225},
                                  .drop_col = {252, 206, 35},
                                  .foam_drop_col = {255, 252, 240},

                                  .g_base = 1.05f,
                                  .drag = 0.020f,
                                  .visc = 0.04f,
                                  .accel_gain = 1.12f,

                                  .foam_init = 1.1f,
                                  .foam_base = 0.65f,
                                  .foam_max = 4.6f,
                                  .foam_agitate = 0.0032f,
                                  .foam_decay = 0.0016f,
                                  .foam_advect = 0.030f,

                                  .drop_thresh = 0.20f,
                                  .drop_chance = 38,
                                  .bubble_rate = 26,
                                  .bubble_speed = 1.20f,
                                  .fine_bubbles = false};

#endif
