// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

void uart2_init(void);
void uart2_write_byte_fast(uint8_t b);
void uart2_write_buf_fast(const uint8_t *data, uint8_t len);
bool uart2_poll_packet(uint8_t *header, uint8_t *param, bool *is_ack);
