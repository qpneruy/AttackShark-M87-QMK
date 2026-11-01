// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "xinmeng_m87.h"

bool sidelight_enabled = true;
bool lightbar_enabled  = true;
bool keylight_enabled  = true;
static uint32_t battery_show_timer = 0;
static bool soft_off_active   = false;
static bool saved_sidelight   = true;
static bool saved_lightbar    = true;
static bool saved_keylight    = true;
// Unified animation timing: 1024ms total (256ms core bloom + 768ms ease-out spread)
#define WAVE_DURATION_FLASH   256
#define WAVE_DURATION_SPREAD  768
#define WAVE_DURATION_TOTAL   (WAVE_DURATION_FLASH + WAVE_DURATION_SPREAD)

enum {
    ANIM_NONE = 0,
    ANIM_WAVE_F8,
    ANIM_EXPAND_GRAVE,
    ANIM_COLLAPSE_GRAVE
};

static uint32_t anim_start_time = 0;
static uint8_t  anim_state      = ANIM_NONE;

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

// Soft power-off collapse timing: 1024ms total (768ms pull + 256ms convergence flash)
#define COLLAPSE_DURATION_PULL  768
#define COLLAPSE_DURATION_FLASH 256
#define COLLAPSE_DURATION_TOTAL (COLLAPSE_DURATION_PULL + COLLAPSE_DURATION_FLASH)

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

static inline void collapse_finish(void) {
    anim_state = ANIM_NONE;
    if (soft_off_active) {
        sidelight_enabled = false;
        lightbar_enabled  = false;
        keylight_enabled  = false;
    }
    rgb_matrix_set_color_all(0, 0, 0);
}

void power_off_collapse_start(void) {
    anim_start_time = timer_read32();
    anim_state      = ANIM_COLLAPSE_GRAVE;
}

bool power_off_collapse_is_active(void) {
    if (anim_state != ANIM_COLLAPSE_GRAVE) {
        return false;
    }
    if (timer_elapsed32(anim_start_time) >= COLLAPSE_DURATION_TOTAL) {
        collapse_finish();
        return false;
    }
    return true;
}

void power_on_wave_start(void) {
    anim_start_time = timer_read32();
    anim_state      = ANIM_WAVE_F8;
}

bool power_on_wave_is_active(void) {
    if (anim_state != ANIM_WAVE_F8) {
        return false;
    }
    if (timer_elapsed32(anim_start_time) >= WAVE_DURATION_TOTAL) {
        anim_state = ANIM_NONE;
        return false;
    }
    return true;
}

void soft_on_expand_start(void) {
    anim_start_time = timer_read32();
    anim_state      = ANIM_EXPAND_GRAVE;
}

bool soft_on_expand_is_active(void) {
    if (anim_state != ANIM_EXPAND_GRAVE) {
        return false;
    }
    if (timer_elapsed32(anim_start_time) >= WAVE_DURATION_TOTAL) {
        anim_state = ANIM_NONE;
        return false;
    }
    return true;
}

static bool expand_effect_render(uint8_t led_min, uint8_t led_max, uint32_t now, const uint8_t *dist_lut) {
    if (!keylight_enabled) {
        anim_state = ANIM_NONE;
        return false;
    }

    uint32_t elapsed = now - anim_start_time;

    if (elapsed >= WAVE_DURATION_TOTAL) {
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
        uint8_t  core_rad = (flash_t >> 2); // 0..63 (covers circular rows around center)
        uint16_t scale    = ((uint16_t)flash_t * max_val) >> 8;

        for (uint8_t i = led_min; i < led_max; i++) {
            uint8_t d = dist_lut[i];
            if (d > core_rad) {
                rgb_matrix_set_color(i, 0, 0, 0);
                continue;
            }
            uint8_t intensity = (d == 0) ? 255 : (uint8_t)((uint16_t)(core_rad - d) << 2);
            uint8_t b         = (uint8_t)(((uint16_t)intensity * scale) >> 8);
            uint8_t g         = b - (b >> 2);
            uint8_t r         = (d == 0) ? g : (b >> 1);
            rgb_matrix_set_color(i, r, g, b);
        }
        return true;
    }

    // Phase 2 (256..1023ms): Outward wave band (64 units thick) from 63 to full board
    uint16_t expand_t   = (uint16_t)(elapsed - WAVE_DURATION_FLASH);
    uint16_t t          = (uint16_t)((expand_t * 85) >> 8); // 0..254
    uint16_t inv_t      = 255 - t;
    uint16_t ease_out   = 255 - ((inv_t * inv_t) >> 8);
    uint16_t reveal_rad = ((uint32_t)ease_out * 245) >> 8;  // Inner reveal boundary: 0..244
    uint16_t front_rad  = reveal_rad + 64;                  // Leading edge: 64..308 (seamless from Phase 1)

    for (uint8_t i = led_min; i < led_max; i++) {
        uint8_t d = dist_lut[i];

        // Passed area: reveal underlying active effect
        if (d < reveal_rad) {
            continue;
        }

        // Unreached area: off
        if (d >= front_rad) {
            rgb_matrix_set_color(i, 0, 0, 0);
            continue;
        }

        // Inside wavefront band (64 units wide)
        uint8_t pos = (uint8_t)(d - reveal_rad); // 0..63
        uint8_t rim = (pos < 48) ? (pos + (pos >> 1)) : ((63 - pos) << 2); // 0..71..0

        uint8_t r = (rim << 1);
        uint8_t g = (rim > 24) ? (uint8_t)((rim - 24) << 2) : 0;
        uint8_t b = (rim >= 63) ? 255 : (rim << 2);

        r = (uint8_t)(((uint16_t)r * max_val) >> 8);
        g = (uint8_t)(((uint16_t)g * max_val) >> 8);
        b = (uint8_t)(((uint16_t)b * max_val) >> 8);

        rgb_matrix_set_color(i, r, g, b);
    }
    return true;
}

static void power_off_collapse_render(uint8_t led_min, uint8_t led_max, uint32_t now) {
    uint32_t elapsed = now - anim_start_time;

    if (elapsed >= COLLAPSE_DURATION_TOTAL) {
        collapse_finish();
        return;
    }

    uint8_t max_val = rgb_matrix_get_val();
    if (max_val < 130) {
        max_val = 130;
    }

    // -------------------------------------------------------------------------
    // Phase 2 (768..1023ms): Convergence flash at ` (Index 16)
    // -------------------------------------------------------------------------
    if (elapsed >= COLLAPSE_DURATION_PULL) {
        uint8_t flash_t   = (uint8_t)(elapsed - COLLAPSE_DURATION_PULL); // 0..255
        uint8_t inv_flash = 255 - flash_t;
        // Core bloom radius shrinks from 32 to 0
        uint8_t core_rad  = (inv_flash >> 3); // 0..31
        uint16_t scale    = ((uint16_t)inv_flash * max_val) >> 8;

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
        return;
    }

    // -------------------------------------------------------------------------
    // Phase 1 (0..767ms): Inward suction collapse to ` (Ease-In)
    // -------------------------------------------------------------------------
    uint16_t t       = (uint16_t)((elapsed * 85) >> 8); // 0..254
    uint16_t ease_in = (t * t) >> 8;
    uint8_t  collapse_radius = (uint8_t)(245 - ((ease_in * 245) >> 8));

    for (uint8_t i = led_min; i < led_max; i++) {
        uint8_t d = led_dist_grave[i];

        // Preserve active effect within boundary
        if (d < collapse_radius) {
            continue;
        }

        // Collapsed area: off
        if (d >= collapse_radius + 32) {
            rgb_matrix_set_color(i, 0, 0, 0);
            continue;
        }

        // Event horizon rim (32-unit width)
        
        uint8_t offset = d - collapse_radius; // 0..31
        uint8_t rim    = 31 - offset;

        uint8_t r = (rim << 2);
        uint8_t g = (rim > 15) ? ((rim - 15) << 4) : 0;
        uint8_t b = (rim << 3);

        r = (uint8_t)(((uint16_t)r * max_val) >> 8);
        g = (uint8_t)(((uint16_t)g * max_val) >> 8);
        b = (uint8_t)(((uint16_t)b * max_val) >> 8);

        rgb_matrix_set_color(i, r, g, b);
    }
}

void load_led_strip_settings(void) {
    if (eeconfig_is_enabled()) {
        uint32_t cfg = eeconfig_read_kb();
        sidelight_enabled = !(cfg & 0x04);
        lightbar_enabled  = !(cfg & 0x08);
        keylight_enabled  = !(cfg & 0x10);
    }
}

void save_led_strip_settings(void) {
    if (eeconfig_is_enabled()) {
        uint32_t cfg = eeconfig_read_kb() & ~0x1Cu;
        if (!sidelight_enabled) cfg |= 0x04;
        if (!lightbar_enabled)  cfg |= 0x08;
        if (!keylight_enabled)  cfg |= 0x10;
        eeconfig_update_kb(cfg);
    }
}

bool m87_is_soft_off(void) {
    return soft_off_active;
}

void m87_enter_soft_off(void) {
    if (soft_off_active) {
        return;
    }

    saved_sidelight = sidelight_enabled;
    saved_lightbar  = lightbar_enabled;
    saved_keylight  = keylight_enabled;

    clear_keyboard();
    SN_GPIO0->BCLR  = MASK_LED_CAPS | MASK_LED_WINLOCK;
    soft_off_active = true;

    if (keylight_enabled && rgb_matrix_is_enabled()) {
        power_off_collapse_start();
    } else {
        sidelight_enabled = false;
        lightbar_enabled  = false;
        keylight_enabled  = false;
        rgb_matrix_set_color_all(0, 0, 0);
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

    if (keylight_enabled && rgb_matrix_is_enabled()) {
        soft_on_expand_start();
    }
}

void keyboard_post_init_kb(void) {
    ch582f_init();
    load_led_strip_settings();
    power_on_wave_start();
    keyboard_post_init_user();
}


void housekeeping_task_kb(void) {
    ch582f_task();

    if (anim_state == ANIM_COLLAPSE_GRAVE && timer_elapsed32(anim_start_time) >= COLLAPSE_DURATION_TOTAL) {
        collapse_finish();
    }

    housekeeping_task_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (soft_off_active) {
        if (record->event.pressed) {
            if (keycode == M87_OFF) {
                m87_exit_soft_off();
                return false;
            }
        }
        if (keycode == MO(1)) {
            return true;
        }
        return false;
    }

    if (!process_record_user(keycode, record) || !ch582f_process_record(keycode, record)) {
        return false;
    }

    if (record->event.pressed) {
        if (keycode == M87_OFF) {
            m87_enter_soft_off();
            return false;
        }
        if (get_highest_layer(layer_state) == 1 && keycode == KC_F8) {
            power_on_wave_start();
            return false;
        }
        switch (keycode) {
            case SIDELIGHT_TOGG:
                sidelight_enabled = !sidelight_enabled;
                save_led_strip_settings();
                return false;
            case LIGHTBAR_TOGG:
                lightbar_enabled = !lightbar_enabled;
                save_led_strip_settings();
                return false;
            case BATTERY_LEVEL:
                ch582f_poll_battery();
                battery_show_timer = timer_read32();
                return false;
            case KEYLIGHT_TOGG:
                keylight_enabled = !keylight_enabled;
                save_led_strip_settings();
                return false;
        }
    }
    return true;
}

#if defined(RGB_MATRIX_ENABLE)
bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }
    uint32_t now = timer_read32();

    if (anim_state == ANIM_COLLAPSE_GRAVE) {
        if ((uint32_t)(now - anim_start_time) >= COLLAPSE_DURATION_TOTAL) {
            collapse_finish();
        } else {
            power_off_collapse_render(led_min, led_max, now);
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
        expand_effect_render(led_min, led_max, now, led_dist_f8);
    } else if (anim_state == ANIM_EXPAND_GRAVE) {
        expand_effect_render(led_min, led_max, now, led_dist_grave);
    } else if (battery_show_timer != 0) {
        if ((uint32_t)(now - battery_show_timer) < 3000) {
            // Only render when intersection with number row keys (17..26) is non-empty
            uint8_t start = (led_min > 17) ? led_min : 17;
            uint8_t end   = (led_max < 27) ? led_max : 27;

            if (start < end) {
                uint8_t pct = ch582f_get_battery_level();

                // Calculate lit_count without division: (pct + 5) * 205 >> 11 ~= (pct + 5) / 10
                uint8_t lit_count = (pct >= 95) ? 10 : (uint8_t)(((uint16_t)(pct + 5) * 205) >> 11);
                if (lit_count == 0) lit_count = 1;

                
                uint8_t r = 0, g = 255;
                if (pct <= 20) {
                    r = 255; g = 0;
                } else if (pct <= 50) {
                    r = 255; g = 180;
                }

                for (uint8_t idx = start; idx < end; idx++) {
                    uint8_t i = idx - 17;
                    if (i < lit_count) {
                        rgb_matrix_set_color(idx, r, g, 0);
                    } else {
                        rgb_matrix_set_color(idx, 0, 0, 0);
                    }
                }
            }
        } else {
            battery_show_timer = 0;
        }
    }

    // -------------------------------------------------------------
    // 2. Mode LED Indicator (Index 87, matrix [4, 14])
    // -------------------------------------------------------------
    if (87 >= led_min && 87 < led_max) {
        m87_mode_t cur_mode = ch582f_get_mode();
        if (cur_mode == M87_MODE_BT || cur_mode == M87_MODE_24G) {
            uint8_t r = 0, g = 0, b = 255;
            if (cur_mode == M87_MODE_24G) {
                r = 0; g = 255; b = 0; // 2.4G = Green
            } else {
                static const uint8_t bt_colors[4][2] = {
                    {0,   0},   // 0: fallback (Blue)
                    {0,   0},   // 1 (BT1): R=0,   G=0,   B=255 (Blue)
                    {0,   255}, // 2 (BT2): R=0,   G=255, B=255 (Cyan)
                    {255, 0}    // 3 (BT3): R=255, G=0,   B=255 (Magenta)
                };
                uint8_t prf = (uint8_t)ch582f_get_bt_profile();
                if (prf > 3) prf = 1;
                r = bt_colors[prf][0];
                g = bt_colors[prf][1];
            }

            if (ch582f_is_pairing()) {
                bool on = (now >> 8) & 1;
                rgb_matrix_set_color(87, on ? r : 0, on ? g : 0, on ? b : 0);
            } else {
                uint32_t conn_time = ch582f_get_connected_time();
                uint32_t elapsed   = now - conn_time;

                if (!ch582f_is_connected()) {
                    if (elapsed < 5000) {
                        bool on = (now >> 9) & 1;
                        rgb_matrix_set_color(87, on ? r : 0, on ? g : 0, on ? b : 0);
                    } else {
                        rgb_matrix_set_color(87, 0, 0, 0);
                    }
                } else {
                    if (elapsed < 2000) {
                        rgb_matrix_set_color(87, r, g, b);
                    } else {
                        rgb_matrix_set_color(87, 0, 0, 0);
                    }
                }
            }
        } else {
            rgb_matrix_set_color(87, 0, 0, 0);
        }
    }

    return true;
}
#endif
