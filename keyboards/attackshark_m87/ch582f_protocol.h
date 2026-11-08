// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <stdint.h>

#define CH582F_CMD_KEYBOARD_REPORT    0xA1
#define CH582F_CMD_CONSUMER_REPORT    0xA3
#define CH582F_CMD_OS_MODE            0xA5
#define CH582F_CMD_SYSTEM             0xA6
#define CH582F_CMD_DEVICE_NAME        0xA9
#define CH582F_CMD_DEVICE_VPID        0xAD

#define CH582F_SYS_WAKEUP             0x00
#define CH582F_SYS_MODE_WIRED         0x11
#define CH582F_SYS_MODE_24G           0x30
#define CH582F_SYS_MODE_BT_BASE       0x30
#define CH582F_SYS_PAIRING_START      0x51
#define CH582F_SYS_POLL_BATTERY       0x53

#define CH582F_OS_MAC                 0x00
#define CH582F_OS_WINDOWS             0x10

#define CH582F_RX_HDR_LED             0x5A
#define CH582F_RX_HDR_STATUS          0x5B
#define CH582F_RX_HDR_BATTERY         0x5C
#define CH582F_RX_HDR_ACK             0x61

#define CH582F_STATUS_PAIRING         0x31
#define CH582F_STATUS_CONNECTED_1     0x32
#define CH582F_STATUS_CONNECTED_2     0x43
#define CH582F_STATUS_DISCONNECTED    0x33
#define CH582F_STATUS_SLEEP           0x42


#define BT_PRF_MASK 0x03