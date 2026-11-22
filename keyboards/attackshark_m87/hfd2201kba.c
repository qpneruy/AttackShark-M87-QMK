// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "ch582f.h"
#include "hfd2201kba.h"
#include "hfd_anim.h"
#include "usb_util.h"
#if defined(VIA_ENABLE)
#    include "via.h"
#endif

static bool    last_usb_active = false;
static uint8_t last_led_bits   = 0xFF;

static void update_hardware_indicators(void) {
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

bool m87_eeconfig_update_masked(const uint32_t value, const uint32_t mask) {
    if (!eeconfig_is_enabled()) {
        return false;
    }

    uint32_t old_cfg = eeconfig_read_kb();
    uint32_t cfg     = (old_cfg & ~mask) | (value & mask);
    if (cfg != old_cfg) {
        eeconfig_update_kb(cfg);
    }
    return true;
}

void m87_load_settings(void) {
    if (!eeconfig_is_enabled()) {
        return;
    }

    uint32_t cfg   = eeconfig_read_kb();
    uint8_t  zones = M87_ZONE_ALL;
    if (cfg & M87_EECFG_SIDELIGHT_OFF) zones &= (uint8_t)~M87_ZONE_SIDELIGHT;
    if (cfg & M87_EECFG_LIGHTBAR_OFF)  zones &= (uint8_t)~M87_ZONE_LIGHTBAR;
    if (cfg & M87_EECFG_KEYLIGHT_OFF)  zones &= (uint8_t)~M87_ZONE_KEYLIGHT;
    m87_led_zone_mask = zones;

    m87_set_wireless_eco(!(cfg & M87_EECFG_WIRELESS_ECO_OFF));
    m87_set_wireless_eco_mins((uint8_t)((cfg >> M87_EECFG_ECO_MIN_SHIFT) & 0xFFu));

#if defined(AUTO_SHIFT_ENABLE)
    if (cfg & M87_EECFG_AUTOSHIFT_ON) {
        autoshift_enable();
    }
    uint8_t as_timeout = (uint8_t)((cfg >> M87_EECFG_AS_TIME_SHIFT) & 0xFFu);
    if (as_timeout >= 80) {
        set_autoshift_timeout(as_timeout);
    }
#endif
}

void m87_save_settings(void) {
    uint8_t  zones = m87_led_zone_mask;
    uint32_t cfg   = 0;

    if (!(zones & M87_ZONE_SIDELIGHT)) cfg |= M87_EECFG_SIDELIGHT_OFF;
    if (!(zones & M87_ZONE_LIGHTBAR))  cfg |= M87_EECFG_LIGHTBAR_OFF;
    if (!(zones & M87_ZONE_KEYLIGHT))  cfg |= M87_EECFG_KEYLIGHT_OFF;
    if (!m87_get_wireless_eco())       cfg |= M87_EECFG_WIRELESS_ECO_OFF;
    cfg |= ((uint32_t)m87_get_wireless_eco_mins() << M87_EECFG_ECO_MIN_SHIFT);

#if defined(AUTO_SHIFT_ENABLE)
    if (get_autoshift_state()) {
        cfg |= M87_EECFG_AUTOSHIFT_ON;
    }
    uint16_t as_t    = get_generic_autoshift_timeout();
    uint8_t  as_byte = (as_t < 80) ? 80 : ((as_t > 255) ? 255 : (uint8_t)as_t);
    cfg |= ((uint32_t)as_byte << M87_EECFG_AS_TIME_SHIFT);
#endif

    const uint32_t mask = M87_EECFG_SIDELIGHT_OFF | M87_EECFG_LIGHTBAR_OFF | M87_EECFG_KEYLIGHT_OFF |
                          M87_EECFG_WIRELESS_ECO_OFF | M87_EECFG_ECO_MIN_MASK |
                          M87_EECFG_AUTOSHIFT_ON | M87_EECFG_AS_TIME_MASK;
    m87_eeconfig_update_masked(cfg, mask);
}

void keyboard_post_init_kb(void) {
    last_usb_active = usb_connected_state();

    SN_GPIO0->BCLR  = (1u << PIN_LED_CAPS) | (1u << PIN_LED_WINLOCK);
    SN_GPIO0->MODE |= (1u << PIN_LED_CAPS) | (1u << PIN_LED_WINLOCK);
    SN_GPIO0->CFG  &= ~(CFG_MASK(PIN_LED_CAPS) | CFG_MASK(PIN_LED_WINLOCK));

    m87_load_settings();
    ch582f_init();
    hfd_anim_init();
    update_hardware_indicators();

    keyboard_post_init_user();
}

void housekeeping_task_kb(void) {
    ch582f_update();
    update_hardware_indicators();

    bool cur_usb_active = usb_connected_state();
    if (cur_usb_active != last_usb_active) {
        last_usb_active = cur_usb_active;
        if (cur_usb_active && !power_on_wave_is_active()) {
            power_on_wave_start();
        }
    }

    hfd_anim_task();

    housekeeping_task_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_user(keycode, record) || !ch582f_process_record(keycode, record)) {
        return false;
    }

    return hfd_anim_process_record(keycode, record);
}

#if defined(VIA_ENABLE)
enum {
    M87_VIA_KEYLIGHT = 1,
    M87_VIA_SIDELIGHT,
    M87_VIA_LIGHTBAR,
    M87_VIA_WIRELESS_ECO,
    M87_VIA_AUTOSHIFT_ENABLE,
    M87_VIA_AUTOSHIFT_TIMEOUT,
    M87_VIA_WIRELESS_TIMEOUT
};

static inline void set_zone_bit(uint8_t bit, bool enable) {
    if (enable) {
        m87_led_zone_mask |= bit;
    } else {
        m87_led_zone_mask &= (uint8_t)~bit;
    }
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    (void)length;
    uint8_t *command_id = &(data[0]);
    uint8_t  channel_id = data[1];
    uint8_t  value_id   = data[2];
    uint8_t *value_data = &(data[3]);

    if (channel_id != id_custom_channel) {
        *command_id = id_unhandled;
        return;
    }

    switch (*command_id) {
        case id_custom_get_value:
            switch (value_id) {
                case M87_VIA_KEYLIGHT:         value_data[0] = (m87_led_zone_mask & M87_ZONE_KEYLIGHT) ? 1 : 0; break;
                case M87_VIA_SIDELIGHT:        value_data[0] = (m87_led_zone_mask & M87_ZONE_SIDELIGHT) ? 1 : 0; break;
                case M87_VIA_LIGHTBAR:         value_data[0] = (m87_led_zone_mask & M87_ZONE_LIGHTBAR) ? 1 : 0; break;
                case M87_VIA_WIRELESS_ECO:     value_data[0] = m87_get_wireless_eco() ? 1 : 0; break;
#    if defined(AUTO_SHIFT_ENABLE)
                case M87_VIA_AUTOSHIFT_ENABLE: value_data[0] = get_autoshift_state() ? 1 : 0; break;
                case M87_VIA_AUTOSHIFT_TIMEOUT: {
                    uint16_t t = get_generic_autoshift_timeout();
                    value_data[0] = (t > 255) ? 255 : (uint8_t)t;
                    break;
                }
#    endif
                case M87_VIA_WIRELESS_TIMEOUT: value_data[0] = m87_get_wireless_eco_mins(); break;
                default:
                    *command_id = id_unhandled;
                    break;
            }
            break;

        case id_custom_set_value:
            switch (value_id) {
                case M87_VIA_KEYLIGHT:         set_zone_bit(M87_ZONE_KEYLIGHT, value_data[0] != 0); break;
                case M87_VIA_SIDELIGHT:        set_zone_bit(M87_ZONE_SIDELIGHT, value_data[0] != 0); break;
                case M87_VIA_LIGHTBAR:         set_zone_bit(M87_ZONE_LIGHTBAR, value_data[0] != 0); break;
                case M87_VIA_WIRELESS_ECO:     m87_set_wireless_eco(value_data[0] != 0); break;
#    if defined(AUTO_SHIFT_ENABLE)
                case M87_VIA_AUTOSHIFT_ENABLE:
                    if (value_data[0]) {
                        autoshift_enable();
                    } else {
                        autoshift_disable();
                    }
                    break;
                case M87_VIA_AUTOSHIFT_TIMEOUT:
                    set_autoshift_timeout((value_data[0] < 80) ? 80 : value_data[0]);
                    break;
#    endif
                case M87_VIA_WIRELESS_TIMEOUT:
                    m87_set_wireless_eco_mins(value_data[0]);
                    break;
                default:
                    *command_id = id_unhandled;
                    break;
            }
            break;

        case id_custom_save:
            m87_save_settings();
            break;

        default:
            *command_id = id_unhandled;
            break;
    }
}
#endif
