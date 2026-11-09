// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ch582f_uart.h"

#include "SN32F240B.h"
#include "ch582f_protocol.h"
#include "wait.h"
#include "timer.h"

static bool is_rx_header(uint8_t b);

void uart2_init(void) {
    sleeping                         = false;
    *(volatile uint32_t *)0x40042000 = 0x009FFFBA;
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
    while (!(SN_UART2->LS & (1 << 5)) && --uart_timeout) {
    }
    SN_UART2->TH = b;
}

bool uart2_read_byte_fast(uint8_t *b) {
    if (SN_UART2->LS & 1) {
        *b = (uint8_t)(SN_UART2->RB & 0xFF);
        return true;
    }
    return false;
}

void parse_rx_byte(const uint8_t b) {
    static uint8_t rx_buf[3];
    static uint8_t rx_idx = 0;

    if (rx_idx == 0) {
        if (is_rx_header(b)) {
            rx_buf[0] = b;
            rx_idx    = 1;
        }
        return;
    }

    rx_buf[rx_idx++] = b;

    if (rx_idx < 3) {
        return;
    }

    uint8_t header = rx_buf[0];
    uint8_t param  = rx_buf[1];
    uint8_t cksum  = rx_buf[2];

    if (header == CH582F_RX_HDR_ACK && param == '\r' && cksum == '\n') {
        sleeping = false;
        rx_idx   = 0;
        return;
    }

    if (cksum == (uint8_t)(header + param)) {
        bool is_sleep_pkt = (header == CH582F_RX_HDR_STATUS) &&
                            (param == CH582F_STATUS_SLEEP || param == CH582F_STATUS_CONNECTED_2);

        if (!is_sleep_pkt) {
            uart2_write_buf_fast((const uint8_t *)"a\r\n", 3);
        }

        if (header == CH582F_RX_HDR_STATUS) {
            if (param == CH582F_STATUS_CONNECTED_1 || param == CH582F_STATUS_CONNECTED_2) {
                if (!is_connected) {
                    is_connected         = true;
                    wireless_state_timer = timer_read32();
                }
                is_pairing = false;
                sleeping   = false;
            } else if (param == CH582F_STATUS_PAIRING) {
                is_pairing           = true;
                is_connected         = false;
                wireless_state_timer = timer_read32();
                sleeping             = false;
            } else if (param == CH582F_STATUS_DISCONNECTED || param == CH582F_STATUS_SLEEP) {
                if (is_connected) {
                    wireless_state_timer = timer_read32();
                }
                is_connected = false;
                is_pairing   = false;
                sleeping     = true;
            }
        } else if (header == CH582F_RX_HDR_LED) {
            if (param < 8) {
                host_led_state = param;
                if (!is_connected) {
                    is_connected         = true;
                    wireless_state_timer = timer_read32();
                }
                is_pairing = false;
            }
            sleeping = false;
        } else if (header == CH582F_RX_HDR_BATTERY) {
            battery_percent = (param > 100) ? 100 : param;
            sleeping        = false;
        } else {
            sleeping = false;
        }

        rx_idx = 0;
        return;
    }

    if (is_rx_header(param)) {
        rx_buf[0] = param;
        rx_buf[1] = cksum;
        rx_idx    = 2;
        return;
    }

    if (is_rx_header(cksum)) {
        rx_buf[0] = cksum;
        rx_idx    = 1;
        return;
    }

    rx_idx = 0;
}

static bool is_rx_header(const uint8_t b) {
    return b == CH582F_RX_HDR_STATUS  ||
           b == CH582F_RX_HDR_LED     ||
           b == CH582F_RX_HDR_BATTERY ||
           b == CH582F_RX_HDR_ACK;
}
