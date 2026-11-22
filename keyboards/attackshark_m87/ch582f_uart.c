// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ch582f_uart.h"
#include "SN32F240B.h"
#include "ch582f_protocol.h"
#include "wait.h"

#define UART_LS_RDR  (1u << 0)
#define UART_LS_THRE (1u << 5)
#define UART_LS_TEMT (1u << 6)

static uint8_t rx_buf[3];
static uint8_t rx_idx = 0;

static bool is_rx_header(uint8_t b);
static bool uart2_read_byte_fast(uint8_t *b);

void uart2_init(void) {
    SN_SYS0->SWDCTRL  |= 1;
    SN_SYS1->AHBCLKEN |= (1 << 18);
    SN_GPIO0->BSET     = 3u;

    SN_UART2->LC  = 0x83;
    SN_UART2->FD  = 0;
    SN_UART2->DLM = 0;
    SN_UART2->DLL = 26; // 48MHz / (16 * 26) = 115200 bps

    SN_UART2->LC       = 0x03;
    SN_UART2->FIFOCTRL = 0x07;
    SN_UART2->CTRL     = 0xC1;

    SN_GPIO0->MODE &= ~0x00000003;
    SN_GPIO0->CFG  &= ~0x0000000F;
    wait_ms(10);
}

void uart2_write_buf_fast(const uint8_t *data, const uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        uart2_write_byte_fast(data[i]);
    }
}

void uart2_write_byte_fast(const uint8_t b) {
    uint32_t uart_timeout = 50000;
    while (!(SN_UART2->LS & UART_LS_THRE) && --uart_timeout) {
    }
    SN_UART2->TH = b;
}

// Blocks (bounded) until the last stop bit has left the shifter.
void uart2_wait_tx_idle(void) {
    uint32_t uart_timeout = 50000;
    while (!(SN_UART2->LS & UART_LS_TEMT) && --uart_timeout) {
    }
}

// Drops RX FIFO contents and any partially parsed frame.
void uart2_flush_rx(void) {
    SN_UART2->FIFOCTRL = 0x07;
    rx_idx             = 0;
}

static bool uart2_read_byte_fast(uint8_t *b) {
    if (SN_UART2->LS & UART_LS_RDR) {
        *b = (uint8_t)(SN_UART2->RB & 0xFF);
        return true;
    }
    return false;
}

bool uart2_poll_packet(uint8_t *header, uint8_t *param, bool *is_ack) {
    uint8_t b;

    while (uart2_read_byte_fast(&b)) {
        if (rx_idx == 0) {
            if (is_rx_header(b)) {
                rx_buf[0] = b;
                rx_idx    = 1;
            }
            continue;
        }

        rx_buf[rx_idx++] = b;
        if (rx_idx < 3) {
            continue;
        }

        uint8_t hdr   = rx_buf[0];
        uint8_t prm   = rx_buf[1];
        uint8_t cksum = rx_buf[2];

        if (hdr == CH582F_RX_HDR_ACK && prm == '\r' && cksum == '\n') {
            *header = hdr;
            *param  = prm;
            *is_ack = true;
            rx_idx  = 0;
            return true;
        }

        if (cksum == (uint8_t)(hdr + prm)) {
            *header = hdr;
            *param  = prm;
            *is_ack = false;
            rx_idx  = 0;
            return true;
        }

        if (is_rx_header(prm)) {
            rx_buf[0] = prm;
            rx_buf[1] = cksum;
            rx_idx    = 2;
        } else if (is_rx_header(cksum)) {
            rx_buf[0] = cksum;
            rx_idx    = 1;
        } else {
            rx_idx = 0;
        }
    }

    return false;
}

static bool is_rx_header(const uint8_t b) {
    return b == CH582F_RX_HDR_STATUS  ||
           b == CH582F_RX_HDR_LED     ||
           b == CH582F_RX_HDR_BATTERY ||
           b == CH582F_RX_HDR_ACK;
}
