// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "action.h"

void hfd_anim_init(void);
void hfd_anim_task(void);
bool hfd_anim_process_record(uint16_t keycode, keyrecord_t *record);
bool hfd_anim_render(uint8_t led_min, uint8_t led_max, uint32_t now);
bool hfd_anim_is_running(void);
void hfd_anim_flush_before_save(void);

bool m87_is_soft_off(void);
void m87_enter_soft_off(void);
void m87_exit_soft_off(void);

#if defined(M87_CUSTOM_ANIMATIONS_ENABLE) && defined(M87_SOFT_OFF_ANIM_ENABLE)
void power_off_collapse_start(void);
bool power_off_collapse_is_active(void);
void soft_on_expand_start(void);
bool soft_on_expand_is_active(void);
#else
static inline void power_off_collapse_start(void) {}
static inline bool power_off_collapse_is_active(void) { return false; }
static inline void soft_on_expand_start(void) {}
static inline bool soft_on_expand_is_active(void) { return false; }
#endif

#if defined(M87_CUSTOM_ANIMATIONS_ENABLE) && defined(M87_WAVE_F8_ENABLE)
void power_on_wave_start(void);
bool power_on_wave_is_active(void);
#else
static inline void power_on_wave_start(void) {}
static inline bool power_on_wave_is_active(void) { return false; }
#endif
