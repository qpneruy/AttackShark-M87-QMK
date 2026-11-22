// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <stdint.h>

// MCU -> CH582F frames: [cmd, payload..., (uint8_t)(cmd + sum(payload))]
#define CH582F_CMD_KEYBOARD_REPORT    0xA1 // mods, 0x00, keys[6]
#define CH582F_CMD_NKRO_REPORT        0xA2 // HID usage bitmap 0x00..0x6F (14 bytes, no mods)
#define CH582F_CMD_CONSUMER_REPORT    0xA3 // usage lo, usage hi
#define CH582F_CMD_SYSTEM_REPORT      0xA4 // bit0 = power down, bit1 = sleep, bit2 = wake up
#define CH582F_CMD_OS_MODE            0xA5
#define CH582F_CMD_CONTROL            0xA6 // 1-byte control code (CH582F_CTRL_*)
#define CH582F_CMD_DEVICE_NAME        0xA9
#define CH582F_CMD_24G_NOTIFY         0xAB // 2.4G only: forwards a 3-byte notify to the dongle
#define CH582F_CMD_DEVICE_VPID        0xAD

#define CH582F_NKRO_BYTES             14

#define CH582F_CTRL_WAKEUP            0x00
#define CH582F_CTRL_MODE_WIRED        0x11
#define CH582F_CTRL_MODE_24G          0x30
#define CH582F_CTRL_MODE_BT_BASE      0x30 // + profile (1..3)
#define CH582F_CTRL_PAIRING_START     0x51
#define CH582F_CTRL_POLL_BATTERY      0x53

#define CH582F_OS_MAC                 0x00
#define CH582F_OS_WINDOWS             0x10

// CH582F -> MCU frames: [hdr, param, (uint8_t)(hdr + param)], or ACK "a\r\n"
#define CH582F_RX_HDR_LED             0x5A
#define CH582F_RX_HDR_STATUS          0x5B
#define CH582F_RX_HDR_BATTERY         0x5C
#define CH582F_RX_HDR_ACK             0x61

// CH582F_RX_HDR_STATUS params (stock ROM 0x43F8..0x44EE)
#define CH582F_STATUS_LOW_BATTERY      0x21
#define CH582F_STATUS_BATTERY_CRITICAL 0x22 // stock: clears low-battery blink, forces sleep in 15s
#define CH582F_STATUS_BATTERY_OK       0x23
#define CH582F_STATUS_PAIRING          0x31
#define CH582F_STATUS_CONNECTED        0x32
#define CH582F_STATUS_DISCONNECTED     0x33 // link lost, CH582F goes to sleep
#define CH582F_STATUS_AWAKE            0x42 // CH582F woke up; not ACKed
#define CH582F_STATUS_IGNORED          0x43 // dropped by stock ROM; not ACKed
