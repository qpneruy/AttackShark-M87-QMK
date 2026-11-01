// Copyright 2022 @art-was-here
// SPDX-License-Identifier: GPL-2.0+

#ifdef ENABLE_RGB_MATRIX_STARLIGHT_SMOOTH
RGB_MATRIX_EFFECT(STARLIGHT_SMOOTH)
#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

hsv_t STARLIGHT_SMOOTH_math(hsv_t hsv, uint8_t i, uint8_t time) {
    // Non-linear S-box hash to prevent diagonal artifacts without SRAM allocation
    uint8_t phase = sin8(i * 167) ^ (i * 53);

    // Dual-hue color offset (+-16 degrees)
    hsv.h += (phase >> 3) - 16;

    // Smooth sine wave brightness
    hsv.v = scale8(abs8(sin8((time + phase) / 2) - 128) * 2, hsv.v);
    return hsv;
}

bool STARLIGHT_SMOOTH(effect_params_t* params) {
    return effect_runner_i(params, &STARLIGHT_SMOOTH_math);
}

#    endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#endif     // ENABLE_RGB_MATRIX_STARLIGHT_SMOOTH
