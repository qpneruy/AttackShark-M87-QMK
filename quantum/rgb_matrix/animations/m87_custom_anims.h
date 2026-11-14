// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#if defined(ENABLE_RGB_MATRIX_AURORA_BOREALIS) || defined(ENABLE_RGB_MATRIX_PLASMA_LAVA) || defined(ENABLE_RGB_MATRIX_EMBER_FIRE) || defined(ENABLE_RGB_MATRIX_METEOR_SHOWER)

#    ifdef ENABLE_RGB_MATRIX_AURORA_BOREALIS
RGB_MATRIX_EFFECT(AURORA_BOREALIS)
#    endif
#    ifdef ENABLE_RGB_MATRIX_PLASMA_LAVA
RGB_MATRIX_EFFECT(PLASMA_LAVA)
#    endif
#    ifdef ENABLE_RGB_MATRIX_EMBER_FIRE
RGB_MATRIX_EFFECT(EMBER_FIRE)
#    endif
#    ifdef ENABLE_RGB_MATRIX_METEOR_SHOWER
RGB_MATRIX_EFFECT(METEOR_SHOWER)
#    endif

#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

// Shared per-frame time state across custom effects (2 bytes RAM total)
static uint8_t s_t1;
static uint8_t s_t2;
// ============================================================================
// 1. AURORA BOREALIS
// ============================================================================
#        ifdef ENABLE_RGB_MATRIX_AURORA_BOREALIS
static hsv_t AURORA_BOREALIS_math(hsv_t hsv, uint8_t i, uint8_t time) {
    (void)time;
    uint8_t x = g_led_config.point[i].x;
    uint8_t y = g_led_config.point[i].y;

    uint8_t w1      = sin8((uint8_t)(x + (sin8((uint8_t)((y << 1) + s_t1)) >> 1) - s_t1));
    uint8_t w2      = sin8((uint8_t)((x << 1) - y + s_t2));
    uint8_t curtain = scale8(w1, w2);

    hsv.h += (uint8_t)((w1 >> 2) - 32);
    hsv.s  = qsub8(hsv.s, curtain >> 2);
    hsv.v  = scale8(qadd8((uint8_t)(90 + scale8(curtain, 165)), curtain >> 2), hsv.v);
    return hsv;
}

bool AURORA_BOREALIS(effect_params_t *params) {
    uint8_t spd = qadd8(rgb_matrix_config.speed >> 3, 2);
    s_t1        = (uint8_t)scale16by8(g_rgb_timer, spd);
    s_t2        = (uint8_t)scale16by8(g_rgb_timer, qadd8(spd, spd >> 1));
    return effect_runner_i(params, &AURORA_BOREALIS_math);
}
#        endif

// ============================================================================
// 2. PLASMA LAVA
// ============================================================================
#        ifdef ENABLE_RGB_MATRIX_PLASMA_LAVA
static hsv_t PLASMA_LAVA_math(hsv_t hsv, uint8_t i, uint8_t time) {
    (void)time;
    uint8_t x = g_led_config.point[i].x;
    uint8_t y = g_led_config.point[i].y;

    uint8_t dx   = (x > 118) ? (x - 118) : (118 - x);
    uint8_t dy   = ((y > 38) ? (y - 38) : (38 - y)) << 1;
    uint8_t dist = sqrt16((uint16_t)(dx * dx) + (uint16_t)(dy * dy));

    uint8_t p1     = sin8((uint8_t)((x << 1) + s_t1));
    uint8_t p2     = sin8((uint8_t)(y * 3 - s_t2));
    uint8_t p3     = sin8((uint8_t)((dist << 1) - s_t1));
    uint8_t energy = (uint8_t)(((uint16_t)p1 + p2 + p3) * 171u >> 9);

    hsv.h += (energy >> 1);
    hsv.v  = scale8((uint8_t)(110 + scale8(energy, 145)), hsv.v);
    return hsv;
}

bool PLASMA_LAVA(effect_params_t *params) {
    uint8_t spd = qadd8(rgb_matrix_config.speed >> 3, 2);
    s_t1        = (uint8_t)scale16by8(g_rgb_timer, spd);
    s_t2        = (uint8_t)scale16by8(g_rgb_timer, qadd8(spd, 3));
    return effect_runner_i(params, &PLASMA_LAVA_math);
}
#        endif

// ============================================================================
// 3. EMBER FIRE
// ============================================================================
#        ifdef ENABLE_RGB_MATRIX_EMBER_FIRE
static hsv_t EMBER_FIRE_math(hsv_t hsv, uint8_t i, uint8_t time) {
    (void)time;
    uint8_t x = g_led_config.point[i].x;
    uint8_t y = g_led_config.point[i].y;

    uint8_t f1    = sin8((uint8_t)((x >> 1) + (y << 1) + s_t1));
    uint8_t f2    = cos8((uint8_t)(x + y + s_t2));
    uint8_t flame = (uint8_t)(((uint16_t)f1 + f2) >> 1);

    uint8_t drop = (y < 70) ? (uint8_t)((70 - y) + ((70 - y) >> 1)) : 0;
    uint8_t heat = qsub8((uint8_t)(125 + scale8(flame, 130)), drop);

    hsv.h += scale8(heat, 26);
    if (heat > 220) {
        hsv.s = qsub8(hsv.s, (uint8_t)((heat - 220) * 3));
    }
    hsv.v = scale8((uint8_t)(85 + scale8(heat, 170)), hsv.v);
    return hsv;
}

bool EMBER_FIRE(effect_params_t *params) {
    uint8_t spd = qadd8(rgb_matrix_config.speed >> 3, 2);
    s_t1        = (uint8_t)scale16by8(g_rgb_timer, (uint8_t)(spd << 1));
    s_t2        = (uint8_t)scale16by8(g_rgb_timer, (uint8_t)(spd * 3));
    return effect_runner_i(params, &EMBER_FIRE_math);
}
#        endif

// ============================================================================
// 4. METEOR SHOWER
// ============================================================================
#        ifdef ENABLE_RGB_MATRIX_METEOR_SHOWER
static hsv_t METEOR_SHOWER_math(hsv_t hsv, uint8_t i, uint8_t time) {
    (void)time;
    uint8_t x    = g_led_config.point[i].x;
    uint8_t lane = g_led_config.point[i].y >> 3;

    uint8_t lane_phase = (uint8_t)(lane * 107u + (sin8((uint8_t)(lane * 59u)) >> 1));
    uint8_t pos        = (uint8_t)(s_t1 - x + lane_phase);

    uint8_t intensity;
    if (pos < 16) {
        intensity = 255;
        hsv.s     = scale8(hsv.s, (uint8_t)(pos << 4));
    } else if (pos < 105) {
        uint8_t fade = 255 - (uint8_t)(((uint16_t)(pos - 16) * 183u) >> 6);
        uint8_t tail = (uint8_t)(((uint16_t)fade + scale8(fade, fade)) >> 1);
        intensity    = (uint8_t)(55 + scale8(tail, 200));
        hsv.h       += (pos >> 2);
    } else {
        intensity = (sin8((uint8_t)(i * 97u + s_t2)) > 210) ? 105 : 55;
    }

    hsv.v = scale8(intensity, hsv.v);
    return hsv;
}

bool METEOR_SHOWER(effect_params_t *params) {
    uint8_t spd = qadd8(rgb_matrix_config.speed >> 2, 4);
    s_t1        = (uint8_t)scale16by8(g_rgb_timer, spd);
    s_t2        = (uint8_t)scale16by8(g_rgb_timer, qadd8(spd >> 2, 1));
    return effect_runner_i(params, &METEOR_SHOWER_math);
}
#        endif

#    endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#endif
