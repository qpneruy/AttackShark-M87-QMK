// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "ch582f.h"
#include "hfd2201kba.h"
#include "hfd_anim.h"
#include "usb_util.h"

#define LED_CFG_SIDELIGHT (1u << 2) // 0x04
#define LED_CFG_LIGHTBAR  (1u << 3) // 0x08
#define LED_CFG_KEYLIGHT  (1u << 4) // 0x10
#define LED_CFG_MASK      (LED_CFG_SIDELIGHT | LED_CFG_LIGHTBAR | LED_CFG_KEYLIGHT)

bool sidelight_enabled = true;
bool lightbar_enabled  = true;
bool keylight_enabled  = true;

static uint32_t battery_show_timer = 0;
static bool     last_vbus_state    = false;
static uint8_t  last_led_bits      = 0xFF;

static void update_hardware_indicators(void);

#if defined(RGB_MATRIX_ENABLE)
static void render_battery_indicator(uint8_t led_min, uint8_t led_max, uint32_t now);
static void render_wireless_indicator(uint8_t led_min, uint8_t led_max, uint32_t now);
#endif

static void load_led_strip_settings(void);
static void save_led_strip_settings(void);

void keyboard_post_init_kb(void) {
    SN_GPIO0->BCLR  = (1u << PIN_LED_CAPS) | (1u << PIN_LED_WINLOCK);
    SN_GPIO0->MODE |= (1u << PIN_LED_CAPS) | (1u << PIN_LED_WINLOCK);
    SN_GPIO0->CFG  &= ~(CFG_MASK(PIN_LED_CAPS) | CFG_MASK(PIN_LED_WINLOCK));

    last_vbus_state = usb_connected_state();

    ch582f_init();
    load_led_strip_settings();
    hfd_anim_init();

    keyboard_post_init_user();
}

void housekeeping_task_kb(void) {
    connect_mode_t prev_mode = ch582f_get_mode();
    ch582f_update();

    if (ch582f_get_mode() != prev_mode && m87_is_soft_off()) {
        m87_exit_soft_off();
    }

    bool cur_vbus = usb_connected_state();
    if (cur_vbus != last_vbus_state) {
        last_vbus_state = cur_vbus;
        if (cur_vbus) {
            if (m87_is_soft_off()) {
                m87_exit_soft_off();
            } else if (!power_on_wave_is_active()) {
                power_on_wave_start();
            }
        }
    }

    update_hardware_indicators();
    hfd_anim_task();

    housekeeping_task_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (m87_is_soft_off()) {
        if (record->event.pressed && keycode == M87_OFF) {
            m87_exit_soft_off();
            return false;
        }
        if (IS_QK_MOMENTARY(keycode) || IS_QK_LAYER_MOD(keycode)) {
            return true;
        }
        return false;
    }

    if (!process_record_user(keycode, record) || !ch582f_process_record(keycode, record)) {
        return false;
    }

    if (!hfd_anim_process_record(keycode, record)) {
        return false;
    }

    if (record->event.pressed) {
        switch (keycode) {
            case BATTERY_LEVEL:
                ch582f_poll_battery();
                battery_show_timer = timer_read32();
                return false;

            case M87_RGB_SAVE:
                hfd_anim_flush_before_save();
#if defined(RGB_MATRIX_ENABLE)
                eeconfig_force_flush_rgb_matrix();
#endif
                save_led_strip_settings();
                return false;
        }
    }

    return true;
}

static void update_hardware_indicators(void) {
    if (m87_is_soft_off()) {
        SN_GPIO0->BCLR = (1u << PIN_LED_CAPS) | (1u << PIN_LED_WINLOCK);
        last_led_bits  = 0xFF;
        return;
    }

    uint8_t cur_leds = (host_keyboard_led_state().caps_lock ? 1 : 0) | (keymap_config.no_gui ? 2 : 0);
    if (cur_leds != last_led_bits) {
        last_led_bits = cur_leds;
        if (cur_leds & 1) {
            SN_GPIO0->BSET = (1u << PIN_LED_CAPS);
        } else {
            SN_GPIO0->BCLR = (1u << PIN_LED_CAPS);
        }

        if (cur_leds & 2) {
            SN_GPIO0->BSET = (1u << PIN_LED_WINLOCK);
        } else {
            SN_GPIO0->BCLR = (1u << PIN_LED_WINLOCK);
        }
    }
}

#if defined(RGB_MATRIX_ENABLE)
bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }

    uint32_t now = timer_read32();

    if (hfd_anim_render(led_min, led_max, now)) {
        return true;
    }

    if (!hfd_anim_is_running() && battery_show_timer != 0) {
        render_battery_indicator(led_min, led_max, now);
    }

    render_wireless_indicator(led_min, led_max, now);
    return true;
}

static void render_battery_indicator(uint8_t led_min, uint8_t led_max, uint32_t now) {
    if (now - battery_show_timer >= 3000) {
        battery_show_timer = 0;
        return;
    }

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

static void load_led_strip_settings(void) {
    if (eeconfig_is_enabled()) {
        uint32_t cfg      = eeconfig_read_kb();
        sidelight_enabled = !(cfg & LED_CFG_SIDELIGHT);
        lightbar_enabled  = !(cfg & LED_CFG_LIGHTBAR);
        keylight_enabled  = !(cfg & LED_CFG_KEYLIGHT);
    }
}

static void save_led_strip_settings(void) {
    if (eeconfig_is_enabled()) {
        uint32_t old_cfg = eeconfig_read_kb();
        uint32_t cfg     = old_cfg & ~LED_CFG_MASK;

        if (!sidelight_enabled) cfg |= LED_CFG_SIDELIGHT;
        if (!lightbar_enabled)  cfg |= LED_CFG_LIGHTBAR;
        if (!keylight_enabled)  cfg |= LED_CFG_KEYLIGHT;

        if (cfg != old_cfg) {
            eeconfig_update_kb(cfg);
        }
    }
}