// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hfd_anim.h"
#include "quantum.h"
#include "hfd2201kba.h"
#include "ch582f.h"

// ==========================================================================
// 0. FEATURE FLAGS NORMALIZATION
// ==========================================================================

#if !defined(M87_CUSTOM_ANIMATIONS_ENABLE) || !defined(RGB_MATRIX_ENABLE)
#    undef M87_WAVE_F8_ENABLE
#    undef M87_RGB_INERTIA_ENABLE
#endif

// ==========================================================================
// 1. CONSTANTS & LOOKUP TABLES
// ==========================================================================

#define WIRELESS_ECO_DEFAULT_MINS 5
#define RGB_MODE_COOLDOWN_MS      260

#if defined(M87_WAVE_F8_ENABLE)
#    define WAVE_DURATION_FLASH   256
#    define WAVE_DURATION_SPREAD  768
#    define WAVE_DURATION_TOTAL   (WAVE_DURATION_FLASH + WAVE_DURATION_SPREAD)
#endif

#if defined(M87_RGB_INERTIA_ENABLE)
#    define RGB_ADJ_COOLDOWN_MS   110
#    define RGB_INERTIA_TICK_MS   10
#endif

#if defined(M87_WAVE_F8_ENABLE)
/* Precomputed distance table from F8 (Index 8) for all LEDs (scaled 0..240) */
static const uint8_t led_dist_f8[RGB_MATRIX_LED_COUNT] = {
    182, 159, 137, 114,  91,  68,  46,  23,   0,  23,  46,  68,  91, 137, 159, 182,
    185, 163, 140, 118,  96,  75,  56,  39,  32,  39,  56,  75,  96, 118, 140, 163, 185,
    193, 172, 151, 130, 111,  93,  78,  67,  63,  64,  78,  93, 111, 130, 151, 172, 193,
    204, 184, 165, 147, 130, 115, 103,  95,  93,  95, 103, 115, 147,
    210, 185, 169, 154, 142, 133, 127, 124, 127, 133, 142, 169, 202,
    240, 223, 208, 163, 163, 170, 181, 193, 208, 223, 240,
    185,
    209, 224,
    177, 193
};
#endif

// ==========================================================================
// 2. STATE VARIABLES
// ==========================================================================

volatile uint8_t      m87_led_zone_mask       = M87_ZONE_ALL;
static bool           wireless_eco_enabled    = true;
static uint8_t        wireless_eco_mins       = WIRELESS_ECO_DEFAULT_MINS;
static bool           battery_show_active     = false;
static deferred_token battery_hide_token      = INVALID_DEFERRED_TOKEN;
static uint32_t       rgb_mode_cooldown_timer = 0;

#if defined(M87_WAVE_F8_ENABLE)
static uint32_t       wave_start_time         = 0;
static bool           wave_active             = false;
static bool           allow_curtain_reveal    = true;
#endif

#if defined(M87_RGB_INERTIA_ENABLE)
static uint32_t       rgb_adj_cooldown_timer  = 0;
static uint8_t        rgb_target_hue          = 0;
static uint8_t        rgb_target_val          = 0;
static uint8_t        rgb_target_spd          = 0;
static deferred_token rgb_inertia_token       = INVALID_DEFERRED_TOKEN;
#endif

// ==========================================================================
// 3. PRIVATE PROTOTYPES
// ==========================================================================

#if defined(M87_WAVE_F8_ENABLE)
static bool     expand_effect_render(uint8_t led_min, uint8_t led_max, uint32_t now, const uint8_t *dist_lut, bool is_overlay);
#endif

#if defined(M87_RGB_INERTIA_ENABLE)
static void     rgb_inertia_sync(void);
static void     rgb_inertia_flush(void);
static void     rgb_inertia_step(uint8_t *cfg_val, uint8_t *target_val, void (*step_fn)(void));
static uint8_t  step_toward_u8(uint8_t cur, uint8_t target, uint8_t step);
#endif

#if defined(RGB_MATRIX_ENABLE)
static void     render_battery_indicator(uint8_t led_min, uint8_t led_max);
static void     render_wireless_indicator(uint8_t led_min, uint8_t led_max, uint32_t now);
#endif

// ==========================================================================
// DEFERRED EXECUTION CALLBACKS
// ==========================================================================

static uint32_t battery_hide_callback(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    (void)cb_arg;

    battery_show_active = false;
    battery_hide_token  = INVALID_DEFERRED_TOKEN;
    return 0;
}

#if defined(M87_RGB_INERTIA_ENABLE)
static uint32_t rgb_inertia_callback(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    (void)cb_arg;

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
        rgb_inertia_token = INVALID_DEFERRED_TOKEN;
        return 0;
    }

    return RGB_INERTIA_TICK_MS;
}
#endif

// ==========================================================================
// 4. PUBLIC API & LIFECYCLE HOOKS
// ==========================================================================

bool m87_get_wireless_eco(void) {
    return wireless_eco_enabled;
}

void m87_set_wireless_eco(const bool enable) {
    wireless_eco_enabled = enable;
}

uint8_t m87_get_wireless_eco_mins(void) {
    return wireless_eco_mins;
}

void m87_set_wireless_eco_mins(const uint8_t mins) {
    wireless_eco_mins = (mins >= 1 && mins <= 10) ? mins : WIRELESS_ECO_DEFAULT_MINS;
}

void hfd_anim_init(void) {
#if defined(M87_RGB_INERTIA_ENABLE)
    rgb_inertia_sync();
#endif
#if defined(M87_WAVE_F8_ENABLE)
    power_on_wave_start();
#endif
}

void hfd_anim_task(void) {
#if defined(M87_WAVE_F8_ENABLE)
    if (wave_active && timer_elapsed32(wave_start_time) >= WAVE_DURATION_TOTAL) {
        allow_curtain_reveal = false;
        wave_active          = false;
    }
#endif
}

bool hfd_anim_process_record(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case BATTERY_LEVEL:
        case M87_RGB_SAVE:
        case SIDELIGHT_TOGG:
        case LIGHTBAR_TOGG:
        case KEYLIGHT_TOGG:
#if defined(RGB_MATRIX_ENABLE)
        case RM_NEXT:
        case RM_PREV:
        case RM_TOGG:
        case RM_HUEU:
        case RM_HUED:
        case RM_VALU:
        case RM_VALD:
        case RM_SPDU:
        case RM_SPDD:
#endif
            if (!record->event.pressed) {
                return false;
            }
            break;
        default:
            return true;
    }

    bool can_mode_step = (timer_elapsed32(rgb_mode_cooldown_timer) >= RGB_MODE_COOLDOWN_MS);

    switch (keycode) {
        case BATTERY_LEVEL:
            ch582f_poll_battery();
            battery_show_active = true;
            if (!extend_deferred_exec(battery_hide_token, 3000)) {
                battery_hide_token = defer_exec(3000, battery_hide_callback, NULL);
            }
            return false;

        case M87_RGB_SAVE:
            hfd_anim_flush_before_save();
#if defined(RGB_MATRIX_ENABLE)
            eeconfig_force_flush_rgb_matrix();
#endif
            m87_save_settings();
            return false;

        case SIDELIGHT_TOGG:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                m87_led_zone_mask      ^= M87_ZONE_SIDELIGHT;
            }
            return false;

        case LIGHTBAR_TOGG:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                m87_led_zone_mask      ^= M87_ZONE_LIGHTBAR;
            }
            return false;

        case KEYLIGHT_TOGG:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                m87_led_zone_mask      ^= M87_ZONE_KEYLIGHT;
            }
            return false;

#if defined(RGB_MATRIX_ENABLE)
        case RM_NEXT:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                rgb_matrix_step_noeeprom();
            }
            return false;

        case RM_PREV:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                rgb_matrix_step_reverse_noeeprom();
            }
            return false;

        case RM_TOGG:
            if (can_mode_step) {
                rgb_mode_cooldown_timer = timer_read32();
                rgb_matrix_toggle_noeeprom();
#    if defined(M87_RGB_INERTIA_ENABLE)
                rgb_inertia_sync();
#    endif
            }
            return false;

        case RM_HUEU:
#    if defined(M87_RGB_INERTIA_ENABLE)
            rgb_inertia_step(&rgb_matrix_config.hsv.h, &rgb_target_hue, rgb_matrix_increase_hue_noeeprom);
#    else
            rgb_matrix_increase_hue_noeeprom();
#    endif
            return false;

        case RM_HUED:
#    if defined(M87_RGB_INERTIA_ENABLE)
            rgb_inertia_step(&rgb_matrix_config.hsv.h, &rgb_target_hue, rgb_matrix_decrease_hue_noeeprom);
#    else
            rgb_matrix_decrease_hue_noeeprom();
#    endif
            return false;

        case RM_VALU:
#    if defined(M87_RGB_INERTIA_ENABLE)
            rgb_inertia_step(&rgb_matrix_config.hsv.v, &rgb_target_val, rgb_matrix_increase_val_noeeprom);
#    else
            rgb_matrix_increase_val_noeeprom();
#    endif
            return false;

        case RM_VALD:
#    if defined(M87_RGB_INERTIA_ENABLE)
            rgb_inertia_step(&rgb_matrix_config.hsv.v, &rgb_target_val, rgb_matrix_decrease_val_noeeprom);
#    else
            rgb_matrix_decrease_val_noeeprom();
#    endif
            return false;

        case RM_SPDU:
#    if defined(M87_RGB_INERTIA_ENABLE)
            rgb_inertia_step(&rgb_matrix_config.speed, &rgb_target_spd, rgb_matrix_increase_speed_noeeprom);
#    else
            rgb_matrix_increase_speed_noeeprom();
#    endif
            return false;

        case RM_SPDD:
#    if defined(M87_RGB_INERTIA_ENABLE)
            rgb_inertia_step(&rgb_matrix_config.speed, &rgb_target_spd, rgb_matrix_decrease_speed_noeeprom);
#    else
            rgb_matrix_decrease_speed_noeeprom();
#    endif
            return false;
#endif
        default:
            return true;
    }
}

#if defined(RGB_MATRIX_ENABLE)
bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }

    uint32_t now       = timer_read32();
    bool     eco_sleep = wireless_eco_enabled &&
                         (ch582f_get_mode() != M87_WIRED_MODE) &&
                         (last_input_activity_elapsed() > ((uint32_t)wireless_eco_mins * 60000u));

    if (eco_sleep) {
        for (uint8_t i = led_min; i < led_max; i++) {
            rgb_matrix_set_color(i, 0, 0, 0);
        }
    } else {
        hfd_anim_render(led_min, led_max, now);
    }

    if ((eco_sleep || !hfd_anim_is_running()) && battery_show_active) {
        render_battery_indicator(led_min, led_max);
    }

    render_wireless_indicator(led_min, led_max, now);
    return true;
}
#endif

bool hfd_anim_render(uint8_t led_min, uint8_t led_max, uint32_t now) {
#if defined(M87_WAVE_F8_ENABLE)
    if (wave_active) {
        expand_effect_render(led_min, led_max, now, led_dist_f8, !allow_curtain_reveal);
    }
#else
    (void)led_min;
    (void)led_max;
    (void)now;
#endif

    return false;
}

bool hfd_anim_is_running(void) {
#if defined(M87_WAVE_F8_ENABLE)
    return wave_active;
#else
    return false;
#endif
}

void hfd_anim_flush_before_save(void) {
#if defined(M87_RGB_INERTIA_ENABLE)
    rgb_inertia_flush();
#endif
}

// ==========================================================================
// 5. ANIMATION TRIGGERS
// ==========================================================================

void power_on_wave_start(void) {
#if defined(M87_WAVE_F8_ENABLE)
    wave_start_time = timer_read32();
    wave_active     = true;
#endif
}

bool power_on_wave_is_active(void) {
#if defined(M87_WAVE_F8_ENABLE)
    return wave_active;
#else
    return false;
#endif
}

// ==========================================================================
// 6. EFFECT & INDICATOR RENDERING IMPLEMENTATIONS
// ==========================================================================

#if defined(M87_WAVE_F8_ENABLE)
static bool expand_effect_render(uint8_t led_min, uint8_t led_max, uint32_t now, const uint8_t *dist_lut, bool is_overlay) {
    if (!(m87_led_zone_mask & M87_ZONE_KEYLIGHT)) {
        allow_curtain_reveal = false;
        wave_active          = false;
        return false;
    }

    uint32_t elapsed = now - wave_start_time;
    if (elapsed >= WAVE_DURATION_TOTAL) {
        allow_curtain_reveal = false;
        wave_active          = false;
        return false;
    }

    uint8_t max_val = rgb_matrix_get_val();
    if (max_val < 130) {
        max_val = 130;
    }

    // Phase 1 (0..255ms): Core flash 0..63 around center (F8)
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
#endif

#if defined(RGB_MATRIX_ENABLE)
static void render_battery_indicator(uint8_t led_min, uint8_t led_max) {
    uint8_t start = (led_min > 17) ? led_min : 17;
    uint8_t end   = (led_max < 27) ? led_max : 27;
    if (start >= end) {
        return;
    }

    uint8_t pct       = ch582f_get_battery_level();
    uint8_t lit_count = (pct >= 95) ? 10 : (uint8_t)(((uint16_t)(pct + 5) * 205) >> 11);
    if (lit_count == 0) {
        lit_count = 1;
    }

    uint8_t r = (pct <= 50) ? 255 : 0;
    uint8_t g = (pct <= 20) ? 0   : ((pct <= 50) ? 180 : 255);

    for (uint8_t idx = start; idx < end; idx++) {
        if ((idx - 17) < lit_count) {
            rgb_matrix_set_color(idx, r, g, 0);
        } else {
            rgb_matrix_set_color(idx, 0, 0, 0);
        }
    }
}

static void render_wireless_indicator(uint8_t led_min, uint8_t led_max, uint32_t now) {
    if (87 < led_min || 87 >= led_max) {
        return;
    }

    connect_mode_t cur_mode = ch582f_get_mode();
    if (cur_mode != M87_BT_MODE && cur_mode != M87_24G_MODE) {
        rgb_matrix_set_color(87, 0, 0, 0);
        return;
    }

    uint8_t r = 0, g = 255, b = 0; // 2.4G = Green
    if (cur_mode == M87_BT_MODE) {
        static const uint8_t bt_colors[3][3] = {
            {0,   0,   255}, // BT1: Blue
            {0,   255, 255}, // BT2: Cyan
            {255, 0,   255}  // BT3: Magenta
        };
        uint8_t prf = (uint8_t)ch582f_get_bt_profile();
        if (prf > 2) prf = 0;
        r = bt_colors[prf][0];
        g = bt_colors[prf][1];
        b = bt_colors[prf][2];
    }

    if (ch582f_is_pairing()) {
        bool on = (now >> 8) & 1;
        rgb_matrix_set_color(87, on ? r : 0, on ? g : 0, on ? b : 0);
        return;
    }

    uint32_t elapsed = now - ch582f_get_connected_time();
    if (!ch582f_is_connected()) {
        if (elapsed < 5000) {
            bool on = (now >> 9) & 1;
            rgb_matrix_set_color(87, on ? r : 0, on ? g : 0, on ? b : 0);
        } else {
            rgb_matrix_set_color(87, 0, 0, 0);
        }
    } else if (elapsed < 2000) {
        rgb_matrix_set_color(87, r, g, b);
    } else {
        rgb_matrix_set_color(87, 0, 0, 0);
    }
}
#endif

// ==========================================================================
// 7. INERTIA HELPERS
// ==========================================================================

#if defined(M87_RGB_INERTIA_ENABLE)
static void rgb_inertia_sync(void) {
    cancel_deferred_exec(rgb_inertia_token);
    rgb_inertia_token = INVALID_DEFERRED_TOKEN;
    rgb_target_hue    = rgb_matrix_config.hsv.h;
    rgb_target_val    = rgb_matrix_config.hsv.v;
    rgb_target_spd    = rgb_matrix_config.speed;
}

static void rgb_inertia_flush(void) {
    if (rgb_inertia_token != INVALID_DEFERRED_TOKEN) {
        cancel_deferred_exec(rgb_inertia_token);
        rgb_inertia_token       = INVALID_DEFERRED_TOKEN;
        rgb_matrix_config.hsv.h = rgb_target_hue;
        rgb_matrix_config.hsv.v = rgb_target_val;
        rgb_matrix_config.speed = rgb_target_spd;
    }
}

static void rgb_inertia_step(uint8_t *cfg_val, uint8_t *target_val, void (*step_fn)(void)) {
    if (timer_elapsed32(rgb_adj_cooldown_timer) < RGB_ADJ_COOLDOWN_MS) {
        return;
    }
    rgb_adj_cooldown_timer = timer_read32();
    bool active            = (rgb_inertia_token != INVALID_DEFERRED_TOKEN);
    if (!active) {
        rgb_target_hue = rgb_matrix_config.hsv.h;
        rgb_target_val = rgb_matrix_config.hsv.v;
        rgb_target_spd = rgb_matrix_config.speed;
    }
    uint8_t cur = *cfg_val;
    *cfg_val    = *target_val;
    step_fn();
    *target_val = *cfg_val;
    *cfg_val    = cur;

    if (!active) {
        rgb_inertia_token = defer_exec(RGB_INERTIA_TICK_MS, rgb_inertia_callback, NULL);
    }
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
