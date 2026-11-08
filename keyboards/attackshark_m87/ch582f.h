//
// Created by qpneruy on 11/8/26.
//

#ifndef ATTACKSHARK_M87_QMK_CH582F_H
#define ATTACKSHARK_M87_QMK_CH582F_H
#include "SN32F240B.h"
#include "_wait.h"
#include "hfd2201kba.h"
#include "keyboard.h"
#include "action.h"

// Hardware Pin Bitmasks (Port 0)
// P0.2 = VBUS, P0.3 = CapsLock, P0.4 = WinLock, P0.6 = 2.4G, P0.7 = BT
#define PIN_VBUS        2
#define PIN_SWITCH_24G  6
#define PIN_SWITCH_BT   7
#define PIN_LED_CAPS    3
#define PIN_LED_WINLOCK 4


#define GPIO_CFG_PULL_UP   0u  // 00b: PULL-UP
#define GPIO_CFG_PULL_DOWN 1u  // 01b: PULL-DOWN
#define GPIO_CFG_FLOATING  2u  // 10b: FLOATING
#define GPIO_CFG_MASK      3u  // 11b: TWO BITS MASK

#define CFG_MASK(pin)           (GPIO_CFG_MASK << ((pin) * 2))
#define CFG_SET(pin, mode)      ((mode) << ((pin) * 2))
typedef enum {
    M87_WIRED_MODE = 0,
    M87_BT_MODE    = 1,
    M87_24G_MODE   = 2
} connect_mode_t;

// Bluetooth Profiles
typedef enum {
    M87_BT_PRF_1 = 0,
    M87_BT_PRF_2 = 1,
    M87_BT_PRF_3 = 2
} bt_profile_t;

#define BT_HOLD_TIMEOUT_MS  1000

typedef struct {
    uint16_t keycode;
    uint32_t press_time;
    keypos_t pos;
    bool     triggered;
} long_press_tracker_t;

void           ch582f_init(void);
void           ch582f_update(void);
bool           ch582f_process_record(uint16_t keycode, keyrecord_t *record);
void           ch582f_cancel_press(void);
void           ch582f_set_bt_profile(const bt_profile_t profile);
void           ch582f_start_pairing(void);

connect_mode_t ch582f_get_mode(void);
bt_profile_t   ch582f_get_bt_profile(void);
bool           ch582f_is_pairing(void);
bool           ch582f_is_connected(void);
uint32_t       ch582f_get_connected_time(void);
uint8_t        ch582f_get_battery_level(void);
void           ch582f_poll_battery(void);
#endif // ATTACKSHARK_M87_QMK_CH582F_H
