#ifndef KVASS_H
#define KVASS_H

#include "drink.h"

static const Drink DRINK_KVASS = {.name = "Kvass",
                                  .beer_hi = {148, 72, 18},
                                  .beer_lo = {68, 28, 6},
                                  .foam_hi = {236, 214, 174},
                                  .foam_lo = {186, 140, 88},
                                  .bubble_col = {255, 232, 175},
                                  .drop_col = {155, 78, 20},
                                  .foam_drop_col = {236, 212, 170},

                                  .g_base = 1.18f,
                                  .drag = 0.016f,
                                  .visc = 0.03f,
                                  .accel_gain = 1.25f,

                                  .foam_init = 0.7f,
                                  .foam_base = 0.18f,
                                  .foam_max = 4.5f,
                                  .foam_agitate = 0.0055f,
                                  .foam_decay = 0.0045f,
                                  .foam_advect = 0.035f,

                                  .drop_thresh = 0.16f,
                                  .drop_chance = 48,
                                  .bubble_rate = 42,
                                  .bubble_speed = 1.45f,
                                  .fine_bubbles = false};

#endif
