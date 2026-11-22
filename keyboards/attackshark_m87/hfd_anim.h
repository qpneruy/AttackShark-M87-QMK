// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "action.h"

extern volatile uint8_t m87_led_zone_mask;

void    hfd_anim_init(void);
void    hfd_anim_task(void);
bool    hfd_anim_process_record(uint16_t keycode, keyrecord_t *record);
bool    hfd_anim_render(uint8_t led_min, uint8_t led_max, uint32_t now);
bool    hfd_anim_is_running(void);
void    hfd_anim_flush_before_save(void);

bool    m87_get_wireless_eco(void);
void    m87_set_wireless_eco(bool enable);
uint8_t m87_get_wireless_eco_mins(void);
void    m87_set_wireless_eco_mins(uint8_t mins);

#if defined(M87_CUSTOM_ANIMATIONS_ENABLE) && defined(M87_WAVE_F8_ENABLE)
void power_on_wave_start(void);
bool power_on_wave_is_active(void);
#else
static inline void power_on_wave_start(void) {}
static inline bool power_on_wave_is_active(void) { return false; }
#endif
