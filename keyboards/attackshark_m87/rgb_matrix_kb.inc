// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

// NOTE: Do NOT add #pragma once or include guards; QMK includes this file multiple times.

RGB_MATRIX_EFFECT(SUPERNOVA_SCINTILLATION)
RGB_MATRIX_EFFECT(STARLIGHT_SMOOTH)
RGB_MATRIX_EFFECT(AURORA_BOREALIS)
RGB_MATRIX_EFFECT(PLASMA_LAVA)
RGB_MATRIX_EFFECT(EMBER_FIRE)
RGB_MATRIX_EFFECT(METEOR_SHOWER)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

// Shared per-frame time state across custom effects (6 bytes RAM total)
static uint8_t  s_t1;
static uint8_t  s_t2;
static uint16_t s_m1;
static uint16_t s_m2;

// ============================================================================
// 1. SUPERNOVA SCINTILLATION
// ============================================================================
static hsv_t SUPERNOVA_SCINTILLATION_math(hsv_t hsv, uint8_t i, uint8_t time) {
    (void)time;
    uint8_t phase1 = sin8(i * 167) ^ (i * 53);
    uint8_t phase2 = sin8(i * 89 + 37);

    uint8_t wave1   = sin8(s_t1 + phase1);
    uint8_t wave2   = sin8(s_t2 + phase2);
    uint8_t twinkle = scale8(wave1, wave2);

    // Squared gamma response curve
    twinkle = scale8(twinkle, twinkle);

    // Supernova white core flash at peak intensity
    if (twinkle > 170) {
        uint8_t flash = (twinkle - 170) * 3;
        hsv.s = qsub8(hsv.s, flash);
    }

    hsv.h += (phase1 >> 3) - 16;

    // Ambient glow floor (~20%)
    hsv.v = scale8(qadd8(twinkle, 51), hsv.v);
    return hsv;
}

bool SUPERNOVA_SCINTILLATION(effect_params_t *params) {
    uint8_t spd = qadd8(rgb_matrix_config.speed >> 3, 2);
    s_t1        = (uint8_t)scale16by8(g_rgb_timer, spd);
    s_t2        = (uint8_t)scale16by8(g_rgb_timer, (uint8_t)(spd << 1));
    return effect_runner_i(params, &SUPERNOVA_SCINTILLATION_math);
}

// ============================================================================
// 2. STARLIGHT SMOOTH (0-RAM Dual Hue)
// ============================================================================
static hsv_t STARLIGHT_SMOOTH_math(hsv_t hsv, uint8_t i, uint8_t time) {
    // Non-linear S-box hash to prevent diagonal artifacts without SRAM allocation
    uint8_t phase = sin8(i * 167) ^ (i * 53);

    // Dual-hue color offset (+-16 degrees)
    hsv.h += (phase >> 3) - 16;

    // Smooth sine wave brightness
    hsv.v = scale8(abs8(sin8((time + phase) / 2) - 128) * 2, hsv.v);
    return hsv;
}

bool STARLIGHT_SMOOTH(effect_params_t *params) {
    return effect_runner_i(params, &STARLIGHT_SMOOTH_math);
}

// ============================================================================
// 3. AURORA BOREALIS
// ============================================================================
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

// ============================================================================
// 4. PLASMA LAVA
// ============================================================================
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

// ============================================================================
// 5. EMBER FIRE
// ============================================================================
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

// ============================================================================
// 6. METEOR SHOWER (Anti-aliased head, 512-unit staggered lanes, smooth stars)
// ============================================================================
static hsv_t METEOR_SHOWER_math(hsv_t hsv, uint8_t i, uint8_t time) {
    (void)time;
    uint8_t  x          = g_led_config.point[i].x;
    uint8_t  lane       = g_led_config.point[i].y >> 3;
    uint16_t lane_phase = (uint16_t)lane * 173u;
    uint16_t t_lane     = (lane & 1u) ? s_m2 : s_m1;
    uint16_t pos        = (uint16_t)(t_lane - x + lane_phase) & 0x1FFu;

    uint8_t meteor_v = 0;
    if (pos < 24u) {
        // Anti-aliased leading nose (~1.85 key widths) for smooth key-to-key glide
        uint8_t u = (uint8_t)(pos * 11u);
        meteor_v  = ease8InOutQuad(u);
        hsv.s     = qsub8(hsv.s, scale8(meteor_v, 210u));
    } else if (pos < 184u) {
        // 160-unit (~12-key) perceptual tail fading smoothly into night sky
        uint8_t d   = (uint8_t)(pos - 24u);
        uint8_t lin = (uint8_t)(255u - (((uint16_t)d * 410u) >> 8));
        uint8_t sq  = scale8(lin, lin);
        meteor_v    = (uint8_t)(((uint16_t)sq + scale8(sq, lin)) >> 1);
        if (d < 28u) {
            uint8_t core = (uint8_t)((28u - d) * 9u);
            hsv.s        = qsub8(hsv.s, scale8(core, 210u));
        }
        hsv.h += (d >> 2);
    }

    // Smooth starfield background (no hard binary blink) + screen blend
    uint8_t twinkle   = sin8((uint8_t)(i * 97u + s_t2));
    twinkle           = scale8(twinkle, twinkle);
    twinkle           = scale8(twinkle, twinkle);
    uint8_t bg        = (uint8_t)(18u + scale8(twinkle, 58u));
    uint8_t intensity = (uint8_t)(bg + scale8((uint8_t)(255u - bg), meteor_v));

    hsv.v = scale8(intensity, hsv.v);
    return hsv;
}

bool METEOR_SHOWER(effect_params_t *params) {
    uint8_t spd      = (uint8_t)((rgb_matrix_config.speed >> 3) + 8u);
    uint8_t spd_fast = (uint8_t)(spd + (spd >> 2));
    s_m1             = (uint16_t)((g_rgb_timer * (uint32_t)spd) >> 8);
    s_m2             = (uint16_t)((g_rgb_timer * (uint32_t)spd_fast) >> 8);
    s_t2             = (uint8_t)((g_rgb_timer * (uint32_t)((spd >> 2) + 2u)) >> 8);
    return effect_runner_i(params, &METEOR_SHOWER_math);
}

#endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
