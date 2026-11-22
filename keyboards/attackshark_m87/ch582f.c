// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ch582f.h"
#include <string.h>
#include "ch582f_protocol.h"
#include "ch582f_uart.h"
#include "deferred_exec.h"
#include "host.h"
#include "keycode_config.h"

// Raw 8-byte 0xA1 payload is copied straight out of report_keyboard_t.
_Static_assert(sizeof(report_keyboard_t) == 8, "report_keyboard_t must not carry a report_id");

extern void last_matrix_activity_trigger(void);

static struct {
    bool     sleeping;
    uint8_t  host_led_state;
    uint8_t  battery_percent;
    bool     is_connected;
    bool     is_pairing;
    bool     low_battery;
    uint32_t wireless_state_timer;
} ch582f_state = {
    .battery_percent = 100
};

static host_driver_t       *default_host_driver   = NULL;
static connect_mode_t       conn_mode             = M87_WIRED_MODE;
static bt_profile_t         bt_profile            = M87_BT_PRF_1;

static uint32_t             last_tx_time          = 0;
static uint64_t             last_keyboard_rep     = UINT64_MAX;
static uint8_t              last_nkro_bits[CH582F_NKRO_BYTES];

static uint8_t              boot_init_state       = 0;
static connect_mode_t       last_sw_mode          = M87_WIRED_MODE;
static uint8_t              sw_debounce           = 0;
static long_press_tracker_t bt_press              = {0};

static deferred_token       boot_init_token       = INVALID_DEFERRED_TOKEN;
static deferred_token       bt_press_token        = INVALID_DEFERRED_TOKEN;
static deferred_token       pairing_seq_token     = INVALID_DEFERRED_TOKEN;
static deferred_token       pairing_timeout_token = INVALID_DEFERRED_TOKEN;
static uint8_t              pairing_seq_step      = 0;

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
static const uint8_t pair_pkt[] = {
    CH582F_CMD_CONTROL,
    CH582F_CTRL_PAIRING_START,
    (uint8_t)(CH582F_CMD_CONTROL + CH582F_CTRL_PAIRING_START)
};
// 0xAB payloads (stock ROM 0x027C / 0x0324), sent only in 2.4G mode while connected.
static const uint8_t notify_24g_sleep[] = {CH582F_CMD_CONTROL, 0xFE, 0x02};
static const uint8_t notify_24g_wake[]  = {CH582F_CMD_CONTROL, 0xFF, 0x01};

static void           ch582f_transport_mode(connect_mode_t n_mode);
static void           cancel_boot_init(void);
static void           cancel_pairing_timers(void);
static void           ch582f_write_frame(uint8_t cmd, const uint8_t *data, uint8_t len);
static void           ch582f_send_report(uint8_t cmd, const uint8_t *data, uint8_t len);
static void           ch582f_send_sys_cmd(uint8_t code);
static void           ch582f_handle_rx_packet(uint8_t header, uint8_t param, bool is_ack);

static uint8_t        ch582f_driver_leds(void);
static void           ch582f_driver_send_keyboard(report_keyboard_t *report);
static void           ch582f_driver_send_nkro(report_nkro_t *report);
static void           ch582f_driver_send_mouse(report_mouse_t *report);
static void           ch582f_driver_send_extra(report_extra_t *report);
static void           ch582f_driver_send_raw_hid(uint8_t *data, uint8_t length);

static void           ch582f_send_keyboard(report_keyboard_t *report);
static void           ch582f_wake_if_needed(void);

static connect_mode_t get_hardware_switch_mode(void);

static const host_driver_t ch582f_host_driver = {
    .keyboard_leds = ch582f_driver_leds,
    .send_keyboard = ch582f_driver_send_keyboard,
    .send_nkro     = ch582f_driver_send_nkro,
    .send_mouse    = ch582f_driver_send_mouse,
    .send_extra    = ch582f_driver_send_extra,
    .send_raw_hid  = ch582f_driver_send_raw_hid
};

// [cmd, data..., (uint8_t)(cmd + sum(data))]
static void ch582f_write_frame(const uint8_t cmd, const uint8_t *data, const uint8_t len) {
    uint8_t sum = cmd;
    uart2_write_byte_fast(cmd);
    for (uint8_t i = 0; i < len; i++) {
        sum += data[i];
        uart2_write_byte_fast(data[i]);
    }
    uart2_write_byte_fast(sum);
}

// HID report path: wireless only, wakes the CH582F first.
static void ch582f_send_report(const uint8_t cmd, const uint8_t *data, const uint8_t len) {
    if (conn_mode == M87_WIRED_MODE) {
        return;
    }
    ch582f_wake_if_needed();
    ch582f_write_frame(cmd, data, len);
    last_tx_time = timer_read32();
}

static void ch582f_send_sys_cmd(const uint8_t code) {
    ch582f_write_frame(CH582F_CMD_CONTROL, &code, 1);
}

static uint32_t boot_init_callback(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    (void)cb_arg;

    switch (boot_init_state++) {
        case 0:
            uart2_write_buf_fast(vpid_pkt, sizeof(vpid_pkt));
            return 10;
        case 1:
            uart2_write_buf_fast(name_pkt, sizeof(name_pkt));
            return 10;
        case 2:
            uart2_write_buf_fast(win_pkt, sizeof(win_pkt));
            return 10;
        default: {
            uint8_t code = (conn_mode == M87_WIRED_MODE) ? CH582F_CTRL_MODE_WIRED :
                           (conn_mode == M87_24G_MODE)   ? CH582F_CTRL_MODE_24G :
                                                           (uint8_t)(CH582F_CTRL_MODE_BT_BASE + (uint8_t)bt_profile + 1);
            ch582f_send_sys_cmd(code);
            boot_init_token = INVALID_DEFERRED_TOKEN;
            return 0;
        }
    }
}

static uint32_t bt_long_press_callback(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    (void)cb_arg;

    bt_press_token = INVALID_DEFERRED_TOKEN;
    if (bt_press.keycode != 0 && conn_mode == M87_BT_MODE) {
        bt_press.triggered = true;
        ch582f_set_bt_profile((bt_profile_t)(bt_press.keycode - M87_BT1));
        ch582f_start_pairing();
    }
    return 0;
}

static uint32_t pairing_seq_callback(uint32_t trigger_time, void *cb_arg) {
    (void)cb_arg;

    if (conn_mode != M87_BT_MODE) {
        pairing_seq_token = INVALID_DEFERRED_TOKEN;
        return 0;
    }

    last_tx_time = trigger_time;

    switch (pairing_seq_step++) {
        case 0:
            uart2_write_buf_fast(name_pkt, sizeof(name_pkt));
            return 10;
        case 1:
            ch582f_send_sys_cmd((uint8_t)(CH582F_CTRL_MODE_BT_BASE + bt_profile + 1));
            return 20;
        case 2:
            uart2_write_buf_fast(win_pkt, sizeof(win_pkt));
            return 10;
        case 3:
        case 4:
            ch582f_state.is_pairing           = true;
            ch582f_state.wireless_state_timer = trigger_time;
            uart2_write_buf_fast(pair_pkt, sizeof(pair_pkt));
            return 20;
        default:
            ch582f_state.is_pairing           = true;
            ch582f_state.wireless_state_timer = trigger_time;
            uart2_write_buf_fast(pair_pkt, sizeof(pair_pkt));
            pairing_seq_token                 = INVALID_DEFERRED_TOKEN;
            return 0;
    }
}

static uint32_t pairing_timeout_callback(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    (void)cb_arg;

    ch582f_state.is_pairing = false;
    pairing_timeout_token   = INVALID_DEFERRED_TOKEN;
    return 0;
}

static void cancel_boot_init(void) {
    if (boot_init_token != INVALID_DEFERRED_TOKEN) {
        cancel_deferred_exec(boot_init_token);
        boot_init_token = INVALID_DEFERRED_TOKEN;
    }
}

static void cancel_pairing_timers(void) {
    cancel_deferred_exec(pairing_seq_token);
    pairing_seq_token = INVALID_DEFERRED_TOKEN;

    cancel_deferred_exec(pairing_timeout_token);
    pairing_timeout_token = INVALID_DEFERRED_TOKEN;
}

void ch582f_init(void) {
    default_host_driver = host_get_driver();

    // P0.2, P0.6, P0.7 = Inputs (Pull-up for BT/2.4G slide switch, Floating for VBUS sense)
    SN_GPIO0->MODE &= ~((1u << PIN_SWITCH_BT) | (1u << PIN_SWITCH_24G) | (1u << PIN_VBUS));
    SN_GPIO0->CFG  &= ~(CFG_MASK(PIN_SWITCH_BT) | CFG_MASK(PIN_SWITCH_24G) | CFG_MASK(PIN_VBUS));
    SN_GPIO0->CFG  |= CFG_SET(PIN_VBUS, GPIO_CFG_FLOATING);

    keymap_config.swap_lalt_lgui = false;
    keymap_config.swap_ralt_rgui = false;

    uart2_init();

    if (eeconfig_is_enabled()) {
        uint8_t prf = eeconfig_read_kb() & M87_EECFG_BT_PRF_MASK;
        if (prf <= M87_BT_PRF_3) {
            bt_profile = (bt_profile_t)prf;
        }
    }

    boot_init_state                = 0;
    last_tx_time                   = timer_read32();
    ch582f_state.sleeping          = false;
    ch582f_state.host_led_state    = 0;
    ch582f_state.battery_percent   = 100;
    ch582f_state.is_connected      = false;
    ch582f_state.is_pairing        = false;

    last_sw_mode = get_hardware_switch_mode();
    ch582f_transport_mode(last_sw_mode);

    boot_init_token = defer_exec(400, boot_init_callback, NULL);
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

    uint8_t hdr;
    uint8_t param;
    bool    is_ack;
    while (uart2_poll_packet(&hdr, &param, &is_ack)) {
        ch582f_handle_rx_packet(hdr, param, is_ack);
    }
}

bool ch582f_process_record(uint16_t keycode, keyrecord_t *record) {
    if (!record->event.pressed && bt_press.keycode != 0) {
        if (KEYEQ(record->event.key, bt_press.pos)) {
            cancel_deferred_exec(bt_press_token);
            bt_press_token = INVALID_DEFERRED_TOKEN;

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
                bt_press.keycode   = keycode;
                bt_press.pos       = record->event.key;
                bt_press.triggered = false;

                cancel_deferred_exec(bt_press_token);
                bt_press_token = defer_exec(BT_HOLD_TIMEOUT_MS, bt_long_press_callback, NULL);
            }
            return false;

        default:
            return true;
    }
}

void ch582f_cancel_press(void) {
    cancel_deferred_exec(bt_press_token);
    bt_press_token   = INVALID_DEFERRED_TOKEN;
    bt_press.keycode = 0;
}

void ch582f_set_bt_profile(const bt_profile_t profile) {
    cancel_boot_init();
    cancel_pairing_timers();
    ch582f_wake_if_needed();

    bt_profile                        = profile;
    ch582f_state.is_pairing           = false;
    ch582f_state.is_connected         = false;
    ch582f_state.wireless_state_timer = timer_read32();

    m87_eeconfig_update_masked(profile, M87_EECFG_BT_PRF_MASK);

    if (conn_mode == M87_BT_MODE) {
        ch582f_send_sys_cmd((uint8_t)(CH582F_CTRL_MODE_BT_BASE + profile + 1));
        last_tx_time          = timer_read32();
        ch582f_state.sleeping = false;
    }
}

void ch582f_start_pairing(void) {
    if (conn_mode != M87_BT_MODE) {
        return;
    }

    cancel_boot_init();
    cancel_pairing_timers();
    ch582f_wake_if_needed();

    ch582f_state.is_pairing           = true;
    ch582f_state.is_connected         = false;
    ch582f_state.wireless_state_timer = timer_read32();
    last_tx_time                      = ch582f_state.wireless_state_timer;

    uart2_write_buf_fast(vpid_pkt, sizeof(vpid_pkt));
    pairing_seq_step  = 0;
    pairing_seq_token = defer_exec(10, pairing_seq_callback, NULL);

    pairing_timeout_token = defer_exec(150000, pairing_timeout_callback, NULL);
}

void ch582f_poll_battery(void) {
    ch582f_wake_if_needed();
    ch582f_send_sys_cmd(CH582F_CTRL_POLL_BATTERY);
    last_tx_time = timer_read32();
}

connect_mode_t ch582f_get_mode(void) {
    return conn_mode;
}

bt_profile_t ch582f_get_bt_profile(void) {
    return bt_profile;
}

bool ch582f_is_pairing(void) {
    return ch582f_state.is_pairing;
}

bool ch582f_is_connected(void) {
    return (conn_mode == M87_WIRED_MODE) ? true : ch582f_state.is_connected;
}

uint32_t ch582f_get_connected_time(void) {
    return ch582f_state.wireless_state_timer;
}

uint8_t ch582f_get_battery_level(void) {
    return ch582f_state.battery_percent;
}

bool ch582f_is_low_battery(void) {
    return ch582f_state.low_battery;
}

bool ch582f_is_charging(void) {
    return (SN_GPIO0->DATA & (1u << PIN_VBUS)) != 0;
}

void ch582f_prepare_sleep(void) {
    if (conn_mode == M87_24G_MODE && ch582f_state.is_connected) {
        ch582f_write_frame(CH582F_CMD_24G_NOTIFY, notify_24g_sleep, sizeof(notify_24g_sleep));
    }
    uart2_wait_tx_idle();
    // Forces a wakeup command before the first report after MCU sleep (ST is frozen while asleep).
    ch582f_state.sleeping = true;
}

void ch582f_on_wakeup(void) {
    uart2_flush_rx();
    if (conn_mode == M87_24G_MODE && ch582f_state.is_connected) {
        ch582f_write_frame(CH582F_CMD_24G_NOTIFY, notify_24g_wake, sizeof(notify_24g_wake));
    }
}

static void ch582f_transport_mode(const connect_mode_t n_mode) {
    clear_keyboard();
    cancel_boot_init();
    ch582f_cancel_press();
    cancel_pairing_timers();
    ch582f_wake_if_needed();

    conn_mode                         = n_mode;
    ch582f_state.is_pairing           = false;
    ch582f_state.is_connected         = false;
    ch582f_state.sleeping             = false;
    ch582f_state.wireless_state_timer = timer_read32();
    last_tx_time                      = ch582f_state.wireless_state_timer;
    last_keyboard_rep                 = UINT64_MAX;
    memset(last_nkro_bits, 0xFF, sizeof(last_nkro_bits));
    last_matrix_activity_trigger();

    // NKRO in all modes: wireless uses 0xA1 (mods) + 0xA2 (bitmap), see ch582f_driver_send_nkro().
    keymap_config.nkro = true;

    uint8_t mode_code;
    if (n_mode == M87_WIRED_MODE) {
        if (default_host_driver) {
            host_set_driver(default_host_driver);
        }
        mode_code = CH582F_CTRL_MODE_WIRED;
    } else {
        host_set_driver((host_driver_t *)&ch582f_host_driver);
        mode_code = (n_mode == M87_24G_MODE) ? CH582F_CTRL_MODE_24G : (uint8_t)(CH582F_CTRL_MODE_BT_BASE + (uint8_t)bt_profile + 1);
    }

    ch582f_send_sys_cmd(mode_code);
}

static void ch582f_handle_rx_packet(const uint8_t header, const uint8_t param, const bool is_ack) {
    if (is_ack) {
        ch582f_state.sleeping = false;
        return;
    }

    // Stock ROM 0x43F8..0x442C: these two are handled before the ACK path and never ACKed.
    if (header == CH582F_RX_HDR_STATUS) {
        if (param == CH582F_STATUS_IGNORED) {
            return;
        }
        if (param == CH582F_STATUS_AWAKE) {
            ch582f_state.sleeping = false;
            return;
        }
    }

    static const uint8_t ack_pkt[3] = {CH582F_RX_HDR_ACK, '\r', '\n'};
    uart2_write_buf_fast(ack_pkt, sizeof(ack_pkt));

    // Any frame proves the CH582F is awake, except DISCONNECTED (it sleeps right after).
    ch582f_state.sleeping = (header == CH582F_RX_HDR_STATUS && param == CH582F_STATUS_DISCONNECTED);

    if (header == CH582F_RX_HDR_STATUS) {
        switch (param) {
            case CH582F_STATUS_LOW_BATTERY:
                ch582f_state.low_battery = true;
                break;
            case CH582F_STATUS_BATTERY_CRITICAL:
            case CH582F_STATUS_BATTERY_OK:
                ch582f_state.low_battery = false;
                break;
            case CH582F_STATUS_CONNECTED:
                if (!ch582f_state.is_connected) {
                    ch582f_state.is_connected         = true;
                    ch582f_state.wireless_state_timer = timer_read32();
                }
                ch582f_state.is_pairing = false;
                break;
            case CH582F_STATUS_PAIRING:
                ch582f_state.is_pairing           = true;
                ch582f_state.is_connected         = false;
                ch582f_state.wireless_state_timer = timer_read32();
                break;
            case CH582F_STATUS_DISCONNECTED:
                if (ch582f_state.is_connected) {
                    ch582f_state.wireless_state_timer = timer_read32();
                }
                ch582f_state.is_connected = false;
                ch582f_state.is_pairing   = false;
                break;
            default:
                break;
        }
    } else if (header == CH582F_RX_HDR_LED) {
        if (param < 8) {
            ch582f_state.host_led_state = param;
            if (!ch582f_state.is_connected) {
                ch582f_state.is_connected         = true;
                ch582f_state.wireless_state_timer = timer_read32();
            }
            ch582f_state.is_pairing = false;
        }
    } else if (header == CH582F_RX_HDR_BATTERY) {
        ch582f_state.battery_percent = (param > 100) ? 100 : param;
    }
}

static uint8_t ch582f_driver_leds(void) {
    return ch582f_state.host_led_state;
}

static void ch582f_driver_send_keyboard(report_keyboard_t *report) {
    ch582f_send_keyboard(report);
}

// Stock layout: 0xA1 carries mods (here also up to 6 usages >= 0x70 that 0xA2 cannot hold),
// 0xA2 carries report->bits[0..13] 1:1 (usages 0x00..0x6F).
static void ch582f_driver_send_nkro(report_nkro_t *report) {
    if (conn_mode == M87_WIRED_MODE) {
        return;
    }

    report_keyboard_t k_report = {.mods = report->mods};
    uint8_t           count    = 0;
    for (uint8_t code = CH582F_NKRO_BYTES * 8; code < NKRO_REPORT_BITS * 8 && count < KEYBOARD_REPORT_KEYS; code++) {
        if (report->bits[code >> 3] & (1u << (code & 7))) {
            k_report.keys[count++] = code;
        }
    }
    ch582f_send_keyboard(&k_report);

    if (memcmp(last_nkro_bits, report->bits, CH582F_NKRO_BYTES) != 0) {
        memcpy(last_nkro_bits, report->bits, CH582F_NKRO_BYTES);
        ch582f_send_report(CH582F_CMD_NKRO_REPORT, report->bits, CH582F_NKRO_BYTES);
    }
}

static void ch582f_driver_send_mouse(report_mouse_t *report) {
    (void)report;
}

static void ch582f_driver_send_extra(report_extra_t *report) {
    if (report->report_id == REPORT_ID_CONSUMER) {
        uint8_t data[2] = {(uint8_t)report->usage, (uint8_t)(report->usage >> 8)};
        ch582f_send_report(CH582F_CMD_CONSUMER_REPORT, data, sizeof(data));
    } else if (report->report_id == REPORT_ID_SYSTEM) {
        // 0x81 power down -> bit0, 0x82 sleep -> bit1, 0x83 wake up -> bit2, release -> 0
        uint16_t usage = report->usage;
        uint8_t  mask  = (usage >= SYSTEM_POWER_DOWN && usage <= SYSTEM_WAKE_UP) ? (uint8_t)(1u << (usage - SYSTEM_POWER_DOWN)) : 0;
        ch582f_send_report(CH582F_CMD_SYSTEM_REPORT, &mask, 1);
    }
}

static void ch582f_driver_send_raw_hid(uint8_t *data, uint8_t length) {
    if (default_host_driver && default_host_driver->send_raw_hid) {
        default_host_driver->send_raw_hid(data, length);
    }
}

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

    ch582f_send_report(CH582F_CMD_KEYBOARD_REPORT, (const uint8_t *)report, sizeof(*report));
}

static void ch582f_wake_if_needed(void) {
    if (conn_mode == M87_WIRED_MODE) {
        return;
    }

    // Unchanged, hardware-proven: re-wake after 2s of TX silence; 4ms settle so the first report isn't lost.
    if (ch582f_state.sleeping || (last_tx_time != 0 && timer_elapsed32(last_tx_time) >= 2000)) {
        ch582f_send_sys_cmd(CH582F_CTRL_WAKEUP);
        wait_ms(4);
        ch582f_state.sleeping = false;
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
