// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0+

#ifdef ENABLE_RGB_MATRIX_SUPERNOVA_SCINTILLATION
RGB_MATRIX_EFFECT(SUPERNOVA_SCINTILLATION)
#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

static hsv_t SUPERNOVA_SCINTILLATION_math(hsv_t hsv, uint8_t i, uint8_t time) {
    uint8_t spd = qadd8(rgb_matrix_config.speed >> 3, 2);
    uint8_t t1  = scale16by8(g_rgb_timer, spd);
    uint8_t t2  = scale16by8(g_rgb_timer, spd * 2);

    uint8_t phase1 = sin8(i * 167) ^ (i * 53);
    uint8_t phase2 = sin8(i * 89 + 37);

    uint8_t wave1   = sin8(t1 + phase1);
    uint8_t wave2   = sin8(t2 + phase2);
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

bool SUPERNOVA_SCINTILLATION(effect_params_t* params) {
    return effect_runner_i(params, &SUPERNOVA_SCINTILLATION_math);
}

#    endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#endif     // ENABLE_RGB_MATRIX_SUPERNOVA_SCINTILLATION
