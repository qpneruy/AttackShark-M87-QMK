// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "ch582f.h"
#include "hfd2201kba.h"
#include "hfd_anim.h"
#include "usb_util.h"

static bool last_vbus_state = false;

void keyboard_post_init_kb(void) {
    last_vbus_state = usb_connected_state();

    ch582f_init();
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

    hfd_anim_task();

    if (m87_is_soft_off() && !hfd_anim_is_running()) {
        wait_ms(30);
    }

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

    return hfd_anim_process_record(keycode, record);
}