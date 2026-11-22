// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "keycodes.h"

enum m87_custom_keycodes {
    M87_BT1 = QK_KB_0,
    M87_BT2,
    M87_BT3,
    SIDELIGHT_TOGG,
    LIGHTBAR_TOGG,
    BATTERY_LEVEL,
    KEYLIGHT_TOGG,
    M87_RGB_SAVE,
    M87_SAFE_RANGE = QK_KB_15
};

// Unified 32-bit eeconfig_kb layout (all 0s on fresh EEPROM = default state)
#define M87_EECFG_BT_PRF_MASK      0x00000003u
#define M87_EECFG_SIDELIGHT_OFF    (1u << 2)
#define M87_EECFG_LIGHTBAR_OFF     (1u << 3)
#define M87_EECFG_KEYLIGHT_OFF     (1u << 4)
#define M87_EECFG_WIRELESS_ECO_OFF (1u << 5)
#define M87_EECFG_AUTOSHIFT_ON     (1u << 6)
#define M87_EECFG_ECO_MIN_SHIFT    8
#define M87_EECFG_ECO_MIN_MASK     (0xFFu << M87_EECFG_ECO_MIN_SHIFT)
#define M87_EECFG_AS_TIME_SHIFT    16
#define M87_EECFG_AS_TIME_MASK     (0xFFu << M87_EECFG_AS_TIME_SHIFT)

bool m87_eeconfig_update_masked(uint32_t value, uint32_t mask);
void m87_load_settings(void);
void m87_save_settings(void);
void m87_refresh_hardware_indicators(void);
