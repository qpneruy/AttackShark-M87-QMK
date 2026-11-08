// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hfd_anim.h"
#include "quantum.h"
#include "hfd2201kba.h"
#include "ch582f.h"

// ==========================================================================
// 1. CONSTANTS, ENUMS & LOOKUP TABLES
// ==========================================================================

#define WAVE_DURATION_FLASH     256
#define WAVE_DURATION_SPREAD    768
#define WAVE_DURATION_TOTAL     (WAVE_DURATION_FLASH + WAVE_DURATION_SPREAD)

#define COLLAPSE_DURATION_PULL  768
#define COLLAPSE_DURATION_FLASH 256
#define COLLAPSE_DURATION_TOTAL (COLLAPSE_DURATION_PULL + COLLAPSE_DURATION_FLASH)

#define SWEEP_DURATION_TOTAL    640

#define RGB_MODE_COOLDOWN_MS    260
#define RGB_ADJ_COOLDOWN_MS     110
#define RGB_INERTIA_TICK_MS     10

enum {
    ANIM_NONE = 0,
    ANIM_WAVE_F8,
    ANIM_EXPAND_GRAVE,
    ANIM_COLLAPSE_GRAVE,
    ANIM_MODE_SWEEP
};

/* Precomputed distance table from F8 (Index 8) for all 88 LEDs (scaled 0..240) */
static const uint8_t led_dist_f8[88] = {
    182, 159, 137, 114,  91,  68,  46,  23,   0,  23,  46,  68,  91, 137, 159, 182,
    185, 163, 140, 118,  96,  75,  56,  39,  32,  39,  56,  75,  96, 118, 140, 163, 185,
    193, 172, 151, 130, 111,  93,  78,  67,  63,  64,  78,  93, 111, 130, 151, 172, 193,
    204, 184, 165, 147, 130, 115, 103,  95,  93,  95, 103, 115, 147,
    210, 185, 169, 154, 142, 133, 127, 124, 127, 133, 142, 169, 202,
    240, 223, 208, 163, 163, 170, 181, 193, 208, 223, 240,
    185
};

/* Distance table from ` (Index 16) to all 88 LEDs (normalized 0..240) */
static const uint8_t led_dist_grave[88] = {
     20,  25,  35,  47,  60,  74,  87, 101, 115, 129, 143, 157, 172, 200, 214, 228,
      0,  14,  28,  43,  57,  71,  85, 100, 114, 128, 142, 156, 171, 185, 199, 213, 227,
     19,  24,  34,  46,  60,  73,  87, 101, 115, 129, 143, 157, 171, 186, 200, 214, 228,
     38,  41,  47,  57,  68,  80,  93, 106, 120, 134, 147, 161, 189,
     57,  61,  67,  75,  84,  94, 106, 118, 131, 144, 157, 184, 212,
     77,  81,  86, 114, 142, 154, 167, 180, 211, 225, 240,
    212
};

// ==========================================================================
// 2. PRIVATE STATE VARIABLES
// ==========================================================================

extern bool sidelight_enabled;
extern bool lightbar_enabled;
extern bool keylight_enabled;

static bool     soft_off_active         = false;
static bool     saved_sidelight         = true;
static bool     saved_lightbar          = true;
static bool     saved_keylight          = true;
static bool     saved_rgb_enabled       = true;
static bool     allow_curtain_reveal    = true;

static uint32_t anim_start_time         = 0;
static uint8_t  anim_state              = ANIM_NONE;
static int8_t   mode_sweep_dir          = 1;
static bool     mode_sweep_committed    = false;

static uint32_t rgb_mode_cooldown_timer = 0;
static uint32_t rgb_adj_cooldown_timer  = 0;
static uint32_t rgb_inertia_timer       = 0;
static uint8_t  rgb_target_hue          = 0;
static uint8_t  rgb_target_val          = 0;
static uint8_t  rgb_target_spd          = 0;
static bool     rgb_inertia_active      = false;

// ==========================================================================
// 3. PRIVATE PROTOTYPES (THEO THỨ TỰ TỪ TRÊN XUỐNG)
// ==========================================================================

static void     collapse_finish(void);
static bool     expand_effect_render(uint8_t led_min, uint8_t led_max, uint32_t now, const uint8_t *dist_lut, bool is_overlay);
static bool     power_off_collapse_render(uint8_t led_min, uint8_t led_max, uint32_t now);

#if defined(RGB_MATRIX_ENABLE)
static bool     mode_sweep_render(uint8_t led_min, uint8_t led_max, uint32_t now);
static void     mode_sweep_start(int8_t dir);
static void     mode_sweep_commit(void);
static uint16_t calc_sweep_front_pos(uint32_t elapsed);

static void     rgb_inertia_sync(void);
static void     rgb_inertia_flush(void);
static void     rgb_inertia_step(uint8_t *cfg_val, uint8_t *target_val, void (*step_fn)(void));
static uint8_t  step_toward_u8(uint8_t cur, uint8_t target, uint8_t step);
#endif

// ==========================================================================
// 4. PUBLIC API & LIFECYCLE HOOKS (MỨC CAO NHẤT)
// ==========================================================================

void hfd_anim_init(void) {
#if defined(RGB_MATRIX_ENABLE)
    rgb_inertia_sync();
#endif
    power_on_wave_start();
}

void hfd_anim_task(void) {
    if ((anim_state == ANIM_WAVE_F8 || anim_state == ANIM_EXPAND_GRAVE) &&
        timer_elapsed32(anim_start_time) >= WAVE_DURATION_TOTAL) {
        if (anim_state == ANIM_WAVE_F8) {
            allow_curtain_reveal = false;
        }
        anim_state = ANIM_NONE;
    } else if (anim_state == ANIM_COLLAPSE_GRAVE &&
               timer_elapsed32(anim_start_time) >= COLLAPSE_DURATION_TOTAL) {
        collapse_finish();
    }
#if defined(RGB_MATRIX_ENABLE)
    else if (anim_state == ANIM_MODE_SWEEP) {
        uint32_t elapsed = timer_elapsed32(anim_start_time);
        if (elapsed >= SWEEP_DURATION_TOTAL) {
            mode_sweep_commit();
            anim_state = ANIM_NONE;
        } else if (!mode_sweep_committed && elapsed >= 250) {
            if (calc_sweep_front_pos(elapsed) >= 240) {
                mode_sweep_commit();
            }
        }
    }

    if (rgb_inertia_active && timer_elapsed32(rgb_inertia_timer) >= RGB_INERTIA_TICK_MS) {
        rgb_inertia_timer = timer_read32();

        int8_t hue_diff = (int8_t)(rgb_target_hue - rgb_matrix_config.hsv.h);
        if (hue_diff > 0) {
            rgb_matrix_config.hsv.h += (hue_diff > 2) ? 2 : 1;
        } else if (hue_diff < 0) {
            rgb_matrix_config.hsv.h -= (hue_diff < -2) ? 2 : 1;
        }

        rgb_matrix_config.hsv.v = step_toward_u8(rgb_matrix_config.hsv.v, rgb_target_val, 2);
        rgb_matrix_config.speed = step_toward_u8(rgb_matrix_config.speed, rgb_target_spd, 2);

        if (rgb_matrix_config.hsv.h == rgb_target_hue &&
            rgb_matrix_config.hsv.v == rgb_target_val &&
            rgb_matrix_config.speed == rgb_target_spd) {
            rgb_inertia_active = false;
        }
    }
#endif
}

bool hfd_anim_process_record(uint16_t keycode, keyrecord_t *record) {
    if (!record->event.pressed) {
        return true;
    }

    if (keycode == M87_OFF) {
        m87_enter_soft_off();
        return false;
    }

    if (get_highest_layer(layer_state) == 1 && keycode == KC_F8) {
        power_on_wave_start();
        return false;
    }

    bool can_mode_step = (timer_elapsed32(rgb_mode_cooldown_timer) >= RGB_MODE_COOLDOWN_MS);

    switch (keycode) {
        case SIDELIGHT_TOGG:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                sidelight_enabled       = !sidelight_enabled;
            }
            return false;

        case LIGHTBAR_TOGG:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                lightbar_enabled        = !lightbar_enabled;
            }
            return false;

        case KEYLIGHT_TOGG:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                keylight_enabled        = !keylight_enabled;
            }
            return false;

#if defined(RGB_MATRIX_ENABLE)
        case RM_NEXT:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                mode_sweep_start(1);
            }
            return false;

        case RM_PREV:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                mode_sweep_start(-1);
            }
            return false;

        case RM_TOGG:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                rgb_matrix_toggle_noeeprom();
                rgb_inertia_sync();
            }
            return false;

        case RM_HUEU:
            rgb_inertia_step(&rgb_matrix_config.hsv.h, &rgb_target_hue, rgb_matrix_increase_hue_noeeprom);
            return false;

        case RM_HUED:
            rgb_inertia_step(&rgb_matrix_config.hsv.h, &rgb_target_hue, rgb_matrix_decrease_hue_noeeprom);
            return false;

        case RM_VALU:
            rgb_inertia_step(&rgb_matrix_config.hsv.v, &rgb_target_val, rgb_matrix_increase_val_noeeprom);
            return false;

        case RM_VALD:
            rgb_inertia_step(&rgb_matrix_config.hsv.v, &rgb_target_val, rgb_matrix_decrease_val_noeeprom);
            return false;

        case RM_SPDU:
            rgb_inertia_step(&rgb_matrix_config.speed, &rgb_target_spd, rgb_matrix_increase_speed_noeeprom);
            return false;

        case RM_SPDD:
            rgb_inertia_step(&rgb_matrix_config.speed, &rgb_target_spd, rgb_matrix_decrease_speed_noeeprom);
            return false;
#endif
        default:
            return true;
    }
}

bool hfd_anim_render(uint8_t led_min, uint8_t led_max, uint32_t now) {
    if (anim_state == ANIM_COLLAPSE_GRAVE) {
        if (power_off_collapse_render(led_min, led_max, now)) {
            return true;
        }
    }

    if (soft_off_active) {
        for (uint8_t i = led_min; i < led_max; i++) {
            rgb_matrix_set_color(i, 0, 0, 0);
        }
        return true;
    }

    if (anim_state == ANIM_WAVE_F8) {
        expand_effect_render(led_min, led_max, now, led_dist_f8, !allow_curtain_reveal);
    } else if (anim_state == ANIM_EXPAND_GRAVE) {
        expand_effect_render(led_min, led_max, now, led_dist_grave, false);
    }
#if defined(RGB_MATRIX_ENABLE)
    else if (anim_state == ANIM_MODE_SWEEP) {
        mode_sweep_render(led_min, led_max, now);
    }
#endif

    return false;
}

bool hfd_anim_is_running(void) {
    return anim_state != ANIM_NONE;
}

void hfd_anim_flush_before_save(void) {
#if defined(RGB_MATRIX_ENABLE)
    if (anim_state == ANIM_MODE_SWEEP) {
        mode_sweep_commit();
    }
    rgb_inertia_flush();
#endif
}

// ==========================================================================
// 5. SOFT-OFF & ANIMATION TRIGGERS
// ==========================================================================

bool m87_is_soft_off(void) {
    return soft_off_active;
}

void m87_enter_soft_off(void) {
    if (soft_off_active) {
        return;
    }

    hfd_anim_flush_before_save();

    saved_sidelight   = sidelight_enabled;
    saved_lightbar    = lightbar_enabled;
    saved_keylight    = keylight_enabled;
    saved_rgb_enabled = rgb_matrix_is_enabled();

    clear_keyboard();
    ch582f_cancel_press();
    SN_GPIO0->BCLR  = (1u << PIN_LED_CAPS) | (1u << PIN_LED_WINLOCK);
    soft_off_active = true;

    if (keylight_enabled && saved_rgb_enabled) {
        power_off_collapse_start();
    } else {
        collapse_finish();
    }
}

void m87_exit_soft_off(void) {
    if (!soft_off_active) {
        return;
    }

    sidelight_enabled = saved_sidelight;
    lightbar_enabled  = saved_lightbar;
    keylight_enabled  = saved_keylight;
    soft_off_active   = false;

    if (saved_rgb_enabled) {
        rgb_matrix_enable_noeeprom();
    }

#if defined(RGB_MATRIX_ENABLE)
    rgb_inertia_sync();
#endif

    if (keylight_enabled && saved_rgb_enabled) {
        soft_on_expand_start();
    }
}

void power_off_collapse_start(void) {
    anim_start_time = timer_read32();
    anim_state      = ANIM_COLLAPSE_GRAVE;
}

bool power_off_collapse_is_active(void) {
    return anim_state == ANIM_COLLAPSE_GRAVE;
}

void power_on_wave_start(void) {
    anim_start_time = timer_read32();
    anim_state      = ANIM_WAVE_F8;
}

bool power_on_wave_is_active(void) {
    return anim_state == ANIM_WAVE_F8;
}

void soft_on_expand_start(void) {
    anim_start_time = timer_read32();
    anim_state      = ANIM_EXPAND_GRAVE;
}

bool soft_on_expand_is_active(void) {
    return anim_state == ANIM_EXPAND_GRAVE;
}

// ==========================================================================
// 6. EFFECT RENDERING IMPLEMENTATIONS
// ==========================================================================

static void collapse_finish(void) {
    anim_state = ANIM_NONE;
    if (soft_off_active) {
        sidelight_enabled = false;
        lightbar_enabled  = false;
        keylight_enabled  = false;
        rgb_matrix_set_color_all(0, 0, 0);
        rgb_matrix_disable_noeeprom();
    } else {
        rgb_matrix_set_color_all(0, 0, 0);
    }
}

static bool expand_effect_render(uint8_t led_min, uint8_t led_max, uint32_t now, const uint8_t *dist_lut, bool is_overlay) {
    if (!keylight_enabled) {
        if (anim_state == ANIM_WAVE_F8) {
            allow_curtain_reveal = false;
        }
        anim_state = ANIM_NONE;
        return false;
    }

    uint32_t elapsed = now - anim_start_time;
    if (elapsed >= WAVE_DURATION_TOTAL) {
        if (anim_state == ANIM_WAVE_F8) {
            allow_curtain_reveal = false;
        }
        anim_state = ANIM_NONE;
        return false;
    }

    uint8_t max_val = rgb_matrix_get_val();
    if (max_val < 130) {
        max_val = 130;
    }

    // Phase 1 (0..255ms): Core flash 0..63 around center (F8 or `)
    if (elapsed < WAVE_DURATION_FLASH) {
        uint8_t  flash_t  = (uint8_t)elapsed;
        uint8_t  core_rad = (flash_t >> 2);
        uint16_t scale    = ((uint16_t)flash_t * max_val) >> 8;

        for (uint8_t i = led_min; i < led_max; i++) {
            uint8_t d = dist_lut[i];
            if (d > core_rad) {
                if (!is_overlay) {
                    rgb_matrix_set_color(i, 0, 0, 0);
                }
                continue;
            }
            uint8_t intensity = (d == 0) ? 255 : (uint8_t)((uint16_t)(core_rad - d) << 2);
            uint8_t b         = (uint8_t)(((uint16_t)intensity * scale) >> 8);
            uint8_t g         = b - (b >> 2);
            uint8_t r         = (d == 0) ? g : (b >> 1);

            if (is_overlay && b < 4) {
                continue;
            }
            rgb_matrix_set_color(i, r, g, b);
        }
        return true;
    }

    // Phase 2 (256..1023ms): Outward wave band (64 units thick)
    uint16_t expand_t   = (uint16_t)(elapsed - WAVE_DURATION_FLASH);
    uint16_t t          = (uint16_t)((expand_t * 85) >> 8);
    uint16_t inv_t      = 255 - t;
    uint16_t ease_out   = 255 - ((inv_t * inv_t) >> 8);
    uint16_t reveal_rad = ((uint32_t)ease_out * 245) >> 8;
    uint16_t front_rad  = reveal_rad + 64;

    for (uint8_t i = led_min; i < led_max; i++) {
        uint8_t d = dist_lut[i];

        if (d < reveal_rad) {
            continue;
        }
        if (d >= front_rad) {
            if (!is_overlay) {
                rgb_matrix_set_color(i, 0, 0, 0);
            }
            continue;
        }

        uint8_t pos = (uint8_t)(d - reveal_rad);
        uint8_t rim = (pos < 48) ? (pos + (pos >> 1)) : ((63 - pos) << 2);

        if (is_overlay && rim < 4) {
            continue;
        }

        uint8_t r = (uint8_t)(((uint16_t)rim * max_val) >> 7);
        uint8_t g = (rim > 24) ? (uint8_t)(((uint16_t)(rim - 24) * max_val) >> 6) : 0;
        uint8_t b = (rim >= 63) ? max_val : (uint8_t)(((uint16_t)rim * max_val) >> 6);

        rgb_matrix_set_color(i, r, g, b);
    }
    return true;
}

static bool power_off_collapse_render(uint8_t led_min, uint8_t led_max, uint32_t now) {
    uint32_t elapsed = now - anim_start_time;
    if (elapsed >= COLLAPSE_DURATION_TOTAL) {
        collapse_finish();
        return false;
    }

    uint8_t max_val = rgb_matrix_get_val();
    if (max_val < 130) {
        max_val = 130;
    }

    // Phase 2 (768..1023ms): Convergence flash at ` (Index 16)
    if (elapsed >= COLLAPSE_DURATION_PULL) {
        uint8_t  flash_t   = (uint8_t)(elapsed - COLLAPSE_DURATION_PULL);
        uint8_t  inv_flash = 255 - flash_t;
        uint8_t  core_rad  = (inv_flash >> 3);
        uint16_t scale     = ((uint16_t)inv_flash * max_val) >> 8;

        for (uint8_t i = led_min; i < led_max; i++) {
            uint8_t d = led_dist_grave[i];
            if (d > core_rad) {
                rgb_matrix_set_color(i, 0, 0, 0);
                continue;
            }

            uint8_t intensity = (d == 0) ? 255 : (uint8_t)((uint16_t)(core_rad - d) << 3);
            uint8_t b         = (uint8_t)(((uint16_t)intensity * scale) >> 8);
            uint8_t g         = b - (b >> 2);
            uint8_t r         = (d == 0) ? g : (b >> 1);
            rgb_matrix_set_color(i, r, g, b);
        }
        return true;
    }

    // Phase 1 (0..767ms): Inward suction collapse to ` (Ease-In)
    uint16_t t               = (uint16_t)((elapsed * 85) >> 8);
    uint16_t ease_in         = (t * t) >> 8;
    uint8_t  collapse_radius = (uint8_t)(245 - ((ease_in * 245) >> 8));

    for (uint8_t i = led_min; i < led_max; i++) {
        uint8_t d = led_dist_grave[i];

        if (d < collapse_radius) {
            continue;
        }
        if (d >= collapse_radius + 32) {
            rgb_matrix_set_color(i, 0, 0, 0);
            continue;
        }

        uint8_t offset = d - collapse_radius;
        uint8_t rim    = 31 - offset;

        uint8_t r = (uint8_t)(((uint16_t)rim * max_val) >> 6);
        uint8_t g = (rim > 15) ? (uint8_t)(((uint16_t)(rim - 15) * max_val) >> 4) : 0;
        uint8_t b = (uint8_t)(((uint16_t)rim * max_val) >> 5);

        rgb_matrix_set_color(i, r, g, b);
    }
    return true;
}

#if defined(RGB_MATRIX_ENABLE)
static bool mode_sweep_render(uint8_t led_min, uint8_t led_max, uint32_t now) {
    if (!keylight_enabled) {
        mode_sweep_commit();
        anim_state = ANIM_NONE;
        return false;
    }

    uint32_t elapsed = now - anim_start_time;
    if (elapsed >= SWEEP_DURATION_TOTAL) {
        mode_sweep_commit();
        anim_state = ANIM_NONE;
        return false;
    }

    uint16_t front_pos = calc_sweep_front_pos(elapsed);
    if (front_pos >= 240 && !mode_sweep_committed) {
        mode_sweep_commit();
    }

    uint8_t max_val = rgb_matrix_get_val();
    if (max_val < 130) {
        max_val = 130;
    }

    for (uint8_t i = led_min; i < led_max; i++) {
        uint8_t d = (mode_sweep_dir >= 0) ? led_dist_grave[i] : (uint8_t)(240 - led_dist_grave[i]);

        if (front_pos < d) {
            continue;
        }

        uint16_t dist_from_front = front_pos - d;
        if (dist_from_front >= 256) {
            continue;
        }

        uint8_t pos = (uint8_t)dist_from_front;
        uint8_t intensity;

        if (pos < 56) {
            intensity = (uint8_t)((pos << 2) + (pos >> 1));
        } else {
            uint16_t rem = 255 - pos;
            uint16_t u   = ((uint16_t)rem * 328) >> 8;
            intensity    = (uint8_t)((u * u) >> 8);
        }

        if (intensity < 6) {
            continue;
        }

        uint8_t b = (uint8_t)(((uint16_t)intensity * max_val) >> 8);
        uint8_t g = (intensity > 36)  ? (uint8_t)(((uint16_t)(intensity - 36) * max_val) >> 8)  : (b >> 2);
        uint8_t r = (intensity > 140) ? (uint8_t)(((uint16_t)(intensity - 140) * max_val) >> 7) : (b >> 2);

        rgb_matrix_set_color(i, r, g, b);
    }
    return true;
}

// ==========================================================================
// 7. INERTIA & SWEEP HELPERS (MỨC THẤP NHẤT)
// ==========================================================================

static void mode_sweep_start(int8_t dir) {
    if (!keylight_enabled || !rgb_matrix_is_enabled()) {
        if (dir >= 0) {
            rgb_matrix_step_noeeprom();
        } else {
            rgb_matrix_step_reverse_noeeprom();
        }
        return;
    }

    if (anim_state == ANIM_MODE_SWEEP && !mode_sweep_committed) {
        mode_sweep_commit();
    }

    mode_sweep_dir       = dir;
    mode_sweep_committed = false;
    anim_start_time      = timer_read32();
    anim_state           = ANIM_MODE_SWEEP;
}

static void mode_sweep_commit(void) {
    if (!mode_sweep_committed) {
        mode_sweep_committed = true;
        if (mode_sweep_dir >= 0) {
            rgb_matrix_step_noeeprom();
        } else {
            rgb_matrix_step_reverse_noeeprom();
        }
    }
}

static uint16_t calc_sweep_front_pos(uint32_t elapsed) {
    uint16_t t = (uint16_t)(((uint32_t)elapsed * 409) >> 10);
    if (t > 255) {
        t = 255;
    }
    uint16_t inv_t    = 255 - t;
    uint16_t ease_out = 255 - ((inv_t * inv_t) >> 8);
    uint16_t progress = (t + ease_out) >> 1;
    return ((uint32_t)progress * 496) >> 8;
}

static void rgb_inertia_sync(void) {
    rgb_target_hue     = rgb_matrix_config.hsv.h;
    rgb_target_val     = rgb_matrix_config.hsv.v;
    rgb_target_spd     = rgb_matrix_config.speed;
    rgb_inertia_active = false;
}

static void rgb_inertia_flush(void) {
    if (rgb_inertia_active) {
        rgb_matrix_config.hsv.h = rgb_target_hue;
        rgb_matrix_config.hsv.v = rgb_target_val;
        rgb_matrix_config.speed = rgb_target_spd;
        rgb_inertia_active      = false;
    }
}

static void rgb_inertia_step(uint8_t *cfg_val, uint8_t *target_val, void (*step_fn)(void)) {
    if (timer_elapsed32(rgb_adj_cooldown_timer) < RGB_ADJ_COOLDOWN_MS) {
        return;
    }
    rgb_adj_cooldown_timer = timer_read32();
    uint8_t cur            = *cfg_val;
    *cfg_val               = rgb_inertia_active ? *target_val : cur;
    step_fn();
    *target_val            = *cfg_val;
    *cfg_val               = cur;
    rgb_inertia_active     = true;
}

static uint8_t step_toward_u8(uint8_t cur, uint8_t target, uint8_t step) {
    if (cur < target) {
        uint8_t diff = target - cur;
        return cur + ((diff < step) ? diff : step);
    } else if (cur > target) {
        uint8_t diff = cur - target;
        return cur - ((diff < step) ? diff : step);
    }
    return cur;
}
#endif