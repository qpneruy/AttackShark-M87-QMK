// Copyright 2023 HorrorTroll
// Modded 2026: Autonomous Wandering & Morphing Lotus Edition
// SPDX-License-Identifier: GPL-2.0+

#ifdef ENABLE_RGB_MATRIX_FLOWER_BLOOMING
RGB_MATRIX_EFFECT(FLOWER_BLOOMING)
#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#include <stdlib.h>

#ifndef abs8
#define abs8(x) ((x) < 0 ? -(x) : (x))
#endif

static hsv_t FLOWER_BLOOMING_math(hsv_t hsv, uint8_t i, uint8_t time) {
    static int16_t pos_x = 112 << 4, pos_y = 32 << 4;
    static int16_t vel_x = 0, vel_y = 0;
    static int16_t tar_x = 112, tar_y = 32;
    static uint8_t dwell = 75;
    static uint8_t bloom_amp = 255;
    static uint8_t last_tick = 0;

    // Fixed physics tick (~62.5 FPS)
    uint8_t tick_60fps = (uint8_t)(g_rgb_timer >> 4);
    if (tick_60fps != last_tick) {
        last_tick = tick_60fps;

        int16_t dtx = tar_x - (pos_x >> 4);
        int16_t dty = tar_y - (pos_y >> 4);
        uint8_t dist_to_tar = sqrt16(dtx * dtx + dty * dty);

        if (dwell > 0) {
            // Phase 1: Stationary bloom
            dwell--;
            vel_x = (vel_x * 13) / 16;
            vel_y = (vel_y * 13) / 16;
            bloom_amp = qadd8(bloom_amp, 8);

            if (dwell == 0) {
                tar_x = 10 + (rand() % 212);
                tar_y = 8 + (rand() % 48);
            }
        } else {
            // Phase 2: Bud closure and glide to target
            if (bloom_amp > 115) bloom_amp = qsub8(bloom_amp, 5);

            int16_t step_dist = dist_to_tar + 1;
            int16_t acc_x = (dtx * 12) / step_dist;
            int16_t acc_y = (dty * 9) / step_dist;

            vel_x = (vel_x * 15 + acc_x) / 16;
            vel_y = (vel_y * 15 + acc_y) / 16;

            if (dist_to_tar < 8) {
                dwell = 75 + (rand() % 45);
            }
        }

        pos_x += vel_x;
        pos_y += vel_y;
    }

    uint8_t smooth_time = scale16by8(g_rgb_timer, qadd8(rgb_matrix_config.speed >> 3, 2));

    int16_t dx = (int16_t)g_led_config.point[i].x - (pos_x >> 4);
    int16_t dy = ((int16_t)g_led_config.point[i].y - (pos_y >> 4)) * 2;

    uint8_t dist  = sqrt16(dx * dx + dy * dy);
    uint8_t angle = atan2_8(dy, dx);

    uint8_t petal1 = sin8(angle * 5 + smooth_time);
    uint8_t petal2 = sin8(angle * 3 - smooth_time * 2);
    uint8_t petal  = scale8(petal1, 155) + scale8(petal2, 100);

    uint8_t bloom   = sin8(dist * 2 - smooth_time * 2 + (petal >> 2));
    uint8_t v_curve = scale8(scale8(bloom, bloom), bloom_amp);

    hsv.v = scale8(qadd8(25, scale8(v_curve, 230)), hsv.v);
    hsv.h += (dist >> 2) + (petal >> 3) + (sin8(smooth_time) >> 2);

    uint8_t core_size = scale8(26 + (sin8(smooth_time * 2) >> 4), bloom_amp);
    if (dist < core_size) {
        uint8_t core = core_size - dist;
        hsv.s = scale8(hsv.s, dist * 5 + 40);
        hsv.v = qadd8(hsv.v, core * 5);
    }

    return hsv;
}

bool FLOWER_BLOOMING(effect_params_t* params) {
    return effect_runner_i(params, &FLOWER_BLOOMING_math);
}

#    endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#endif     // ENABLE_RGB_MATRIX_FLOWER_BLOOMING
