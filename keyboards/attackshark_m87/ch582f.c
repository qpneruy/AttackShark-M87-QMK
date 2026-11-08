// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ch582f.h"
#include "ch582f_protocol.h"
#include "ch582f_uart.h"
#include "host.h"
#include "keycode_config.h"

uint8_t  host_led_state       = 0;
uint8_t  battery_percent      = 100;
bool     sleeping             = false;
bool     is_connected         = false;
bool     is_pairing           = false;
uint32_t wireless_state_timer = 0;

static host_driver_t       *default_host_driver = NULL;
static connect_mode_t       conn_mode           = M87_WIRED_MODE;
static bt_profile_t         bt_profile          = M87_BT_PRF_1;

static uint32_t             last_tx_time        = 0;
static uint64_t             last_keyboard_rep   = UINT64_MAX;

static uint32_t             boot_time           = 0;
static uint8_t              boot_init_state     = 0;
static connect_mode_t       last_sw_mode        = M87_WIRED_MODE;
static uint8_t              sw_debounce         = 0;
static long_press_tracker_t bt_press            = {0};

static const uint8_t vpid_pkt[] = {
    CH582F_CMD_DEVICE_VPID, 0x45, 0x0C, 0x06, 0x80, 0x84
};
static const uint8_t name_pkt[] = {
    CH582F_CMD_DEVICE_NAME, 12, 'M', '8', '7', ' ', 'K', 'e', 'y', 'b', 'o', 'a', 'r', 'd', 0xC2
};
static const uint8_t win_pkt[] = {
    CH582F_CMD_OS_MODE,
    CH582F_OS_WINDOWS,
    (uint8_t)(CH582F_CMD_OS_MODE + CH582F_OS_WINDOWS)
};

static void           ch582f_transport_mode(connect_mode_t n_mode);

static uint8_t        ch582f_driver_leds(void);
static void           ch582f_driver_send_keyboard(report_keyboard_t *report);
static void           ch582f_driver_send_nkro(report_nkro_t *report);
static void           ch582f_driver_send_mouse(report_mouse_t *report);
static void           ch582f_driver_send_extra(report_extra_t *report);
static void           ch582f_driver_send_raw_hid(uint8_t *data, uint8_t length);

static void           ch582f_send_keyboard(report_keyboard_t *report);
static void           ch582f_send_consumer(uint16_t usage);
static void           ch582f_wake_if_needed(void);

static connect_mode_t get_hardware_switch_mode(void);
static bool           update_eeconfig(uint32_t n_cfg, uint32_t mask);

static const host_driver_t ch582f_host_driver = {
    .keyboard_leds = ch582f_driver_leds,
    .send_keyboard = ch582f_driver_send_keyboard,
    .send_nkro     = ch582f_driver_send_nkro,
    .send_mouse    = ch582f_driver_send_mouse,
    .send_extra    = ch582f_driver_send_extra,
    .send_raw_hid  = ch582f_driver_send_raw_hid
};

void ch582f_init(void) {
    default_host_driver = host_get_driver();

    // P0.2, P0.6, P0.7 = Inputs (Pull-up cho công tắc BT/2.4G, Floating cho VBUS)
    SN_GPIO0->MODE &= ~((1u << PIN_SWITCH_BT) | (1u << PIN_SWITCH_24G) | (1u << PIN_VBUS));
    SN_GPIO0->CFG  &= ~(CFG_MASK(PIN_SWITCH_BT) | CFG_MASK(PIN_SWITCH_24G) | CFG_MASK(PIN_VBUS));
    SN_GPIO0->CFG  |= CFG_SET(PIN_VBUS, GPIO_CFG_FLOATING);

    keymap_config.swap_lalt_lgui = false;
    keymap_config.swap_ralt_rgui = false;

    uart2_init();

    if (eeconfig_is_enabled()) {
        uint8_t prf = eeconfig_read_kb() & BT_PRF_MASK;
        if (prf <= M87_BT_PRF_3) {
            bt_profile = (bt_profile_t)prf;
        }
    }

    boot_time       = timer_read32();
    boot_init_state = 0;
    last_tx_time    = boot_time;
    sleeping        = false;

    last_sw_mode = get_hardware_switch_mode();
    ch582f_transport_mode(last_sw_mode);
}

void ch582f_update(void) {
    connect_mode_t sw_mode = get_hardware_switch_mode();
    if (sw_mode != last_sw_mode) {
        last_sw_mode = sw_mode;
        sw_debounce  = 20;
    } else if (sw_debounce > 0 && --sw_debounce == 0) {
        if (sw_mode != conn_mode) {
            ch582f_transport_mode(sw_mode);
        }
    }

    host_driver_t *cur_drv = host_get_driver();
    if (!default_host_driver && cur_drv != &ch582f_host_driver) {
        default_host_driver = cur_drv;
    }
    host_driver_t *target_drv = (conn_mode == M87_WIRED_MODE) ? default_host_driver : (host_driver_t *)&ch582f_host_driver;
    if (target_drv && cur_drv != target_drv) {
        host_set_driver(target_drv);
    }

    uint8_t rx_b;
    while (uart2_read_byte_fast(&rx_b)) {
        parse_rx_byte(rx_b);
    }

    if (boot_init_state < 4) {
        uint32_t elapsed = timer_elapsed32(boot_time);
        if (boot_init_state == 0 && elapsed >= 400) {
            uart2_write_buf_fast(vpid_pkt, sizeof(vpid_pkt));
            boot_init_state = 1;
        } else if (boot_init_state == 1 && elapsed >= 410) {
            uart2_write_buf_fast(name_pkt, sizeof(name_pkt));
            boot_init_state = 2;
        } else if (boot_init_state == 2 && elapsed >= 420) {
            uart2_write_buf_fast(win_pkt, sizeof(win_pkt));
            boot_init_state = 3;
        } else if (boot_init_state == 3 && elapsed >= 430) {
            uint8_t code = (conn_mode == M87_WIRED_MODE) ? CH582F_SYS_MODE_WIRED :
                           ((conn_mode == M87_24G_MODE)  ? CH582F_SYS_MODE_BT_BASE :
                                                           (CH582F_SYS_MODE_BT_BASE + (uint8_t)bt_profile + 1));
            uint8_t pkt[3] = {CH582F_CMD_SYSTEM, code, (uint8_t)(CH582F_CMD_SYSTEM + code)};
            uart2_write_buf_fast(pkt, 3);
            boot_init_state = 4;
        }
    }

    if (is_pairing && timer_elapsed32(wireless_state_timer) >= 150000) {
        is_pairing = false;
    }

    if (bt_press.keycode != 0 && !bt_press.triggered) {
        if (conn_mode != M87_BT_MODE) {
            bt_press.keycode = 0;
        } else if (timer_elapsed32(bt_press.press_time) >= 2000) {
            bt_press.triggered = true;
            ch582f_set_bt_profile((bt_profile_t)(bt_press.keycode - M87_BT1));
            ch582f_start_pairing();
        }
    }
}

bool ch582f_process_record(uint16_t keycode, keyrecord_t *record) {
    if (!record->event.pressed && bt_press.keycode != 0) {
        if (KEYEQ(record->event.key, bt_press.pos)) {
            if (!bt_press.triggered && conn_mode == M87_BT_MODE) {
                bt_profile_t prf = (bt_profile_t)(bt_press.keycode - M87_BT1);
                ch582f_set_bt_profile(prf);
            }
            bt_press.keycode = 0;
            return false;
        }
    }

    switch (keycode) {
        case M87_BT1 ... M87_BT3:
            if (record->event.pressed && conn_mode == M87_BT_MODE) {
                bt_press.keycode    = keycode;
                bt_press.pos        = record->event.key;
                bt_press.press_time = timer_read32();
                bt_press.triggered  = false;
            }
            return false;

        case M87_24G:
            return false;

        default:
            return true;
    }
}

void ch582f_cancel_press(void) {
    bt_press.keycode = 0;
}

void ch582f_set_bt_profile(const bt_profile_t profile) {
    ch582f_wake_if_needed();
    bt_profile           = profile;
    is_pairing           = false;
    is_connected         = false;
    wireless_state_timer = timer_read32();

    update_eeconfig(profile, BT_PRF_MASK);

    if (conn_mode == M87_BT_MODE) {
        uint8_t prf    = CH582F_SYS_MODE_BT_BASE + profile + 1;
        uint8_t pkt[3] = {CH582F_CMD_SYSTEM, prf, (uint8_t)(CH582F_CMD_SYSTEM + prf)};

        uart2_write_buf_fast(pkt, 3);

        last_tx_time = timer_read32();
        sleeping     = false;
    }
}

void ch582f_start_pairing(void) {
    if (conn_mode != M87_BT_MODE) {
        return;
    }

    ch582f_wake_if_needed();
    is_pairing           = true;
    is_connected         = false;
    wireless_state_timer = timer_read32();

    static const uint8_t pair_pkt[] = {
        CH582F_CMD_SYSTEM,
        CH582F_SYS_PAIRING_START,
        (uint8_t)(CH582F_CMD_SYSTEM + CH582F_SYS_PAIRING_START)
    };

    uart2_write_buf_fast(vpid_pkt, sizeof(vpid_pkt));
    wait_ms(10);

    uart2_write_buf_fast(name_pkt, sizeof(name_pkt));
    wait_ms(10);

    uint8_t prof_val    = CH582F_SYS_MODE_BT_BASE + bt_profile + 1;
    uint8_t prof_pkt[3] = {
        CH582F_CMD_SYSTEM,
        prof_val,
        (uint8_t)(CH582F_CMD_SYSTEM + prof_val)
    };
    uart2_write_buf_fast(prof_pkt, sizeof(prof_pkt));
    wait_ms(20);

    uart2_write_buf_fast(win_pkt, sizeof(win_pkt));
    wait_ms(10);

    for (uint8_t i = 0; i < 3; i++) {
        uart2_write_buf_fast(pair_pkt, sizeof(pair_pkt));
        wait_ms(20);
    }

    last_tx_time = timer_read32();
}

void ch582f_poll_battery(void) {
    ch582f_wake_if_needed();
    static const uint8_t pkt[3] = {
        CH582F_CMD_SYSTEM,
        CH582F_SYS_POLL_BATTERY,
        (uint8_t)(CH582F_CMD_SYSTEM + CH582F_SYS_POLL_BATTERY)
    };
    uart2_write_buf_fast(pkt, 3);
    last_tx_time = timer_read32();
}

connect_mode_t ch582f_get_mode(void) {
    return conn_mode;
}

bt_profile_t ch582f_get_bt_profile(void) {
    return bt_profile;
}

bool ch582f_is_pairing(void) {
    return is_pairing;
}

bool ch582f_is_connected(void) {
    return (conn_mode == M87_WIRED_MODE) ? true : is_connected;
}

uint32_t ch582f_get_connected_time(void) {
    return wireless_state_timer;
}

uint8_t ch582f_get_battery_level(void) {
    return battery_percent;
}

// ==========================================================================
// 5. INTERNAL MODE CONTROL LOGIC
// ==========================================================================

static void ch582f_transport_mode(const connect_mode_t n_mode) {
    clear_keyboard();
    ch582f_wake_if_needed();

    conn_mode            = n_mode;
    is_pairing           = false;
    is_connected         = false;
    sleeping             = false;
    wireless_state_timer = timer_read32();
    last_tx_time         = wireless_state_timer;
    last_keyboard_rep    = UINT64_MAX;

    uint8_t pkt[3];
    pkt[0] = CH582F_CMD_SYSTEM;

    if (n_mode == M87_WIRED_MODE) {
        keymap_config.nkro = true;
        if (default_host_driver) {
            host_set_driver(default_host_driver);
        }
        pkt[1] = CH582F_SYS_MODE_WIRED;
        pkt[2] = (uint8_t)(CH582F_CMD_SYSTEM + CH582F_SYS_MODE_WIRED);
    } else {
        keymap_config.nkro = false;
        host_set_driver((host_driver_t *)&ch582f_host_driver);
        pkt[1] = (n_mode == M87_24G_MODE) ? CH582F_SYS_MODE_BT_BASE : (CH582F_SYS_MODE_BT_BASE + (uint8_t)bt_profile + 1);
        pkt[2] = (uint8_t)(CH582F_CMD_SYSTEM + pkt[1]);
    }

    uart2_write_buf_fast(pkt, 3);
}

// ==========================================================================
// 6. QMK HOST DRIVER IMPLEMENTATION
// ==========================================================================

static uint8_t ch582f_driver_leds(void) {
    return host_led_state;
}

static void ch582f_driver_send_keyboard(report_keyboard_t *report) {
    ch582f_send_keyboard(report);
}

static void ch582f_driver_send_nkro(report_nkro_t *report) {
    report_keyboard_t k_report;
    k_report.mods     = report->mods;
    k_report.reserved = 0;

    static const uint8_t ctz_lut[8] = {0, 1, 6, 2, 7, 5, 4, 3};

    uint8_t count = 0;
    for (uint8_t i = 0; i < sizeof(report->bits) && count < 6; i++) {
        uint8_t b = report->bits[i];
        while (b && count < 6) {
            uint8_t isolated       = b & -b;
            uint8_t bit            = ctz_lut[((uint8_t)(isolated * 29) >> 5) & 7];
            k_report.keys[count++] = (i << 3) + bit;
            b &= (b - 1);
        }
    }
    while (count < 6) {
        k_report.keys[count++] = 0;
    }
    ch582f_send_keyboard(&k_report);
}

static void ch582f_driver_send_mouse(report_mouse_t *report) {
    (void)report;
}

static void ch582f_driver_send_extra(report_extra_t *report) {
    if (report->report_id == REPORT_ID_CONSUMER) {
        ch582f_send_consumer(report->usage);
    }
}

static void ch582f_driver_send_raw_hid(uint8_t *data, uint8_t length) {
    if (default_host_driver && default_host_driver->send_raw_hid) {
        default_host_driver->send_raw_hid(data, length);
    }
}

// ==========================================================================
// 7. LOW-LEVEL UART PROTOCOL SENDERS
// ==========================================================================

static void ch582f_send_keyboard(report_keyboard_t *report) {
    if (conn_mode == M87_WIRED_MODE) {
        return;
    }

    report->reserved = 0x00;

    uint64_t cur_rep;
    __builtin_memcpy(&cur_rep, report, sizeof(cur_rep));
    if (cur_rep == last_keyboard_rep) {
        return;
    }
    last_keyboard_rep = cur_rep;

    ch582f_wake_if_needed();

    const uint8_t *raw = (const uint8_t *)report;
    uint8_t        sum = 0xA1;
    for (uint8_t i = 0; i < 8; i++) {
        sum += raw[i];
    }

    uart2_write_byte_fast(0xA1);
    uart2_write_buf_fast(raw, 8);
    uart2_write_byte_fast(sum);

    last_tx_time = timer_read32();
}

static void ch582f_send_consumer(const uint16_t usage) {
    if (conn_mode == M87_WIRED_MODE) {
        return;
    }
    ch582f_wake_if_needed();

    uint8_t lo = (uint8_t)usage;
    uint8_t hi = (uint8_t)(usage >> 8);

    uart2_write_byte_fast(0xA3);
    uart2_write_byte_fast(lo);
    uart2_write_byte_fast(hi);
    uart2_write_byte_fast((uint8_t)(0xA3 + lo + hi));

    last_tx_time = timer_read32();
}

static void ch582f_wake_if_needed(void) {
    if (conn_mode == M87_WIRED_MODE) {
        return;
    }

    if (sleeping || (last_tx_time != 0 && timer_elapsed32(last_tx_time) >= 2000)) {
        static const uint8_t wake_pkt[3] = {0xA6, 0x00, 0xA6};
        uart2_write_buf_fast(wake_pkt, 3);
        wait_ms(4);
        sleeping = false;
    }
}

static connect_mode_t get_hardware_switch_mode(void) {
    uint32_t p0 = SN_GPIO0->DATA;

    if (!(p0 & (1u << PIN_SWITCH_BT))) {
        return M87_BT_MODE;
    }
    if (!(p0 & (1u << PIN_SWITCH_24G))) {
        return M87_24G_MODE;
    }
    return M87_WIRED_MODE;
}

static bool update_eeconfig(const uint32_t n_cfg, const uint32_t mask) {
    if (eeconfig_is_enabled()) {
        uint32_t o_cfg = eeconfig_read_kb();
        uint32_t cfg   = (o_cfg & ~mask) | (n_cfg & mask);

        if (cfg != o_cfg) {
            eeconfig_update_kb(cfg);
        }
        return true;
    }
    return false;
}