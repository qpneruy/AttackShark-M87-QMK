// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

extern bool     sleeping;
extern uint8_t  host_led_state;
extern bool     is_connected;
extern bool     is_pairing;
extern uint32_t wireless_state_timer;
extern uint8_t  battery_percent;

void uart2_init(void);
void uart2_write_byte_fast(const uint8_t b);
void uart2_write_buf_fast(const uint8_t *data, const uint8_t len);
bool uart2_read_byte_fast(uint8_t *b);
void parse_rx_byte(const uint8_t b);