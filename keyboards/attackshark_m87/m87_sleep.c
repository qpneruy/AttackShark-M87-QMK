// Copyright 2026 HuuTinh (@qpneruy)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "m87_sleep.h"

#include <ch.h>
#include <hal.h>

#include "ch582f.h"
#include "hfd2201kba.h"
#include "hfd_anim.h"
#include "keyboard.h"
#include "matrix.h"

#define M87_LED_ANODES_P0_MASK       0xFF18u /* A3, A4, A8..A15 */
#define M87_LED_ANODES_P1_MASK       0x459Fu /* B0..B4, B7, B8, B10, B14 */
#define M87_LED_ANODES_P3_MASK       (1u << 3) /* D3 */

#define M87_MATRIX_COLS_P2_MASK      0x9FFFu /* C0..C12, C15 */
#define M87_MATRIX_COLS_P2_CFG_MASK  0xC3FFFFFFu
#define M87_MATRIX_COLS_P1_MASK      0xB000u /* B12, B13, B15 */
#define M87_MATRIX_COLS_P1_CFG_MASK  0xCF000000u
#define M87_STRIP_CATHODES_P1_MASK   0x0A40u /* B6, B9, B11 */
#define M87_SLEEP_HIGH_P1_MASK       (M87_MATRIX_COLS_P1_MASK | M87_STRIP_CATHODES_P1_MASK)

#define M87_MATRIX_ROWS_P3_MASK      0x0FC0u /* D6..D11 */
#define M87_MATRIX_ROWS_P3_CFG_MASK  0x00FFF000u
#define M87_MATRIX_ROWS_P3_CFG_FLOAT 0x00AAA000u

#define M87_SWITCH_P0_MASK           ((1u << PIN_SWITCH_24G) | (1u << PIN_SWITCH_BT))
#define M87_WAKE_P0_MASK             ((1u << PIN_VBUS) | M87_SWITCH_P0_MASK)

extern void last_matrix_activity_trigger(void);

OSAL_IRQ_HANDLER(SN32_GPIOA_HANDLER) {
    OSAL_IRQ_PROLOGUE();
    SN_GPIO0->IE = 0u;
    SN_GPIO0->IC = 0xFFFFu;
    nvicDisableVector(SN32_GPIOA_NUMBER);
    NVIC_ClearPendingIRQ(P0_IRQn);
    OSAL_IRQ_EPILOGUE();
}

OSAL_IRQ_HANDLER(SN32_GPIOD_HANDLER) {
    OSAL_IRQ_PROLOGUE();
    SN_GPIO3->IE = 0u;
    SN_GPIO3->IC = 0xFFFFu;
    nvicDisableVector(SN32_GPIOD_NUMBER);
    NVIC_ClearPendingIRQ(P3_IRQn);
    OSAL_IRQ_EPILOGUE();
}

static void spin_delay(volatile uint32_t count) {
    while (count-- > 0u) {
        __NOP();
    }
}

static bool m87_any_key_pressed(void) {
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        if (matrix_get_row(row) != 0u) {
            return true;
        }
    }
    return false;
}

static bool m87_sleep_should_enter(void) {
    if (EFLD1.state == FLASH_PGM || EFLD1.state == FLASH_ERASE) {
        return false;
    }
    if (!m87_wireless_eco_active()) {
        return false;
    }
    if ((SN_GPIO0->DATA & (1u << PIN_VBUS)) != 0u || (SN_GPIO0->DATA & M87_SWITCH_P0_MASK) == M87_SWITCH_P0_MASK) {
        return false;
    }
    return !m87_any_key_pressed();
}

static void m87_arm_wake_irqs(void) {
    SN_GPIO3->IS  &= ~M87_MATRIX_ROWS_P3_MASK;
    SN_GPIO3->IBS |= M87_MATRIX_ROWS_P3_MASK;
    SN_GPIO3->IEV &= ~M87_MATRIX_ROWS_P3_MASK;
    SN_GPIO3->IC   = 0xFFFFu;
    SN_GPIO3->IE   = M87_MATRIX_ROWS_P3_MASK;

    SN_GPIO0->IS  &= ~M87_WAKE_P0_MASK;
    SN_GPIO0->IBS |= M87_WAKE_P0_MASK;
    SN_GPIO0->IEV &= ~M87_WAKE_P0_MASK;
    SN_GPIO0->IC   = 0xFFFFu;
    SN_GPIO0->IE   = M87_WAKE_P0_MASK;

    NVIC_ClearPendingIRQ(P3_IRQn);
    nvicEnableVector(SN32_GPIOD_NUMBER, SN32_GPIOD_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(P0_IRQn);
    nvicEnableVector(SN32_GPIOA_NUMBER, SN32_GPIOA_IRQ_PRIORITY);
}

static void m87_disarm_wake_irqs(void) {
    SN_GPIO3->IE = 0u;
    SN_GPIO3->IC = 0xFFFFu;
    nvicDisableVector(SN32_GPIOD_NUMBER);
    NVIC_ClearPendingIRQ(P3_IRQn);

    SN_GPIO0->IE = 0u;
    SN_GPIO0->IC = 0xFFFFu;
    nvicDisableVector(SN32_GPIOA_NUMBER);
    NVIC_ClearPendingIRQ(P0_IRQn);
}

static void m87_deep_sleep_wfi(void) {
    /* Freeze ChibiOS tickless timer (CT16B0 + SysTick) */
    SN_CT16B0->TMRCTRL &= ~1u;
    uint32_t saved_st_ctrl = SysTick->CTRL;
    SysTick->CTRL          = SysTick_CTRL_CLKSOURCE_Msk;
    SCB->ICSR              = SCB_ICSR_PENDSTCLR_Msk;

    /* Power down USB PHY/VREG33 safely (VBUS is guaranteed LOW) */
    bool     usb_clk_on    = (SN_SYS1->AHBCLKEN & (1u << 4)) != 0u;
    uint32_t saved_usb_cfg = 0u;
    uint32_t saved_usb_ie  = 0u;
    if (usb_clk_on) {
        nvicDisableVector(SN32_USB_NUMBER);
        saved_usb_cfg = SN_USB->CFG;
        saved_usb_ie  = SN_USB->INTEN;
        SN_USB->CFG  &= ~(mskPHY_EN | mskESD_EN);
        spin_delay(120u);
    }

    /* Downshift clock to ILRC/4 (8kHz) and enter Deep-Sleep */
    SN_SYS0->CLKCFG = 1u;
    while ((SN_SYS0->CLKCFG & 0x70u) != 0x10u) {
    }
    SN_SYS0->AHBCP    = 2u;
    SN_FLASH->LPCTRL  = 0x5AFA0002u;
    SN_SYS0->ANBCTRL  = 0u;
    if (usb_clk_on) {
        SN_USB->CFG = 0u;
    }

    SN_PMU->CTRL = 2u;
    __DSB();
    __WFI();
    __ISB();
    SN_PMU->CTRL = 0u;

    /* Restore Flash wait-states and 48MHz IHRC */
    SN_FLASH->LPCTRL = 0x5AFA0004u;
    SN_FLASH->LPCTRL = 0x5AFA0005u;
    SN_SYS0->ANBCTRL = 1u;
    while ((SN_SYS0->CSST & 1u) == 0u) {
    }
    SN_SYS0->CLKCFG = 0u;
    while ((SN_SYS0->CLKCFG & 0x70u) != 0u) {
    }
    SN_SYS0->AHBCP &= ~7u;

    /* Restore USB registers and IRQ */
    if (usb_clk_on) {
        SN_USB->CFG    = saved_usb_cfg;
        SN_USB->INSTSC = 0xFFFFFFFFu;
        SN_USB->INTEN  = saved_usb_ie;
        NVIC_ClearPendingIRQ(USB_IRQn);
        nvicEnableVector(SN32_USB_NUMBER, SN32_USB_IRQ_PRIORITY);
    }

    /* Resume ChibiOS tickless timer */
    SN_CT16B0->TMRCTRL |= 1u;
    if ((saved_st_ctrl & (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk)) ==
        (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk)) {
        st_lld_start_alarm(st_lld_get_alarm());
    } else {
        SysTick->CTRL = saved_st_ctrl;
    }
}

void m87_sleep_task(void) {
    if (!m87_sleep_should_enter()) {
        return;
    }

    /* Notify CH582F while 48MHz + UART2 are active before locking interrupts */
    ch582f_prepare_sleep();

    chSysLock();

    uint32_t entry_sw = SN_GPIO0->DATA & M87_SWITCH_P0_MASK;
    if ((SN_GPIO0->DATA & (1u << PIN_VBUS)) != 0u || entry_sw == M87_SWITCH_P0_MASK) {
        chSysUnlock();
        last_matrix_activity_trigger();
        ch582f_on_wakeup();
        return;
    }

    /* Stop CT16B1 (PWMD1) and drive all LED anodes + Caps/Win indicators OFF */
    nvicDisableVector(SN32_CT16B1_NUMBER);
    NVIC_ClearPendingIRQ(CT16B1_IRQn);
    SN_CT16B1->TMRCTRL    = 0u;
    uint32_t saved_pwmenb = SN_CT16B1->PWMENB;
    SN_CT16B1->PWMIOENB   = 0u;
    SN_CT16B1->PWMENB     = 0u;
    SN_CT16B1->IC         = 0x01FFFFFFu;
    SN_SYS1->AHBCLKEN    &= ~(1u << 7);

    SN_GPIO0->BCLR = M87_LED_ANODES_P0_MASK;
    SN_GPIO1->BCLR = M87_LED_ANODES_P1_MASK;
    SN_GPIO3->BCLR = M87_LED_ANODES_P3_MASK;

    /* Drive all 17 matrix cols + 3 strip cathodes HIGH */
    SN_GPIO2->BSET  = M87_MATRIX_COLS_P2_MASK;
    SN_GPIO2->MODE |= M87_MATRIX_COLS_P2_MASK;
    SN_GPIO1->BSET  = M87_SLEEP_HIGH_P1_MASK;
    SN_GPIO1->MODE |= M87_SLEEP_HIGH_P1_MASK;

    /* Switch 6 matrix rows P3.6..P3.11 to floating input and let external pull-downs settle */
    SN_GPIO3->MODE &= ~M87_MATRIX_ROWS_P3_MASK;
    SN_GPIO3->CFG   = (SN_GPIO3->CFG & ~M87_MATRIX_ROWS_P3_CFG_MASK) | M87_MATRIX_ROWS_P3_CFG_FLOAT;
    spin_delay(1000u);

    m87_arm_wake_irqs();

    /* Race-free check after arming wake IRQs */
    if ((SN_GPIO3->DATA & M87_MATRIX_ROWS_P3_MASK) == 0u &&
        (SN_GPIO0->DATA & (1u << PIN_VBUS)) == 0u &&
        (SN_GPIO0->DATA & M87_SWITCH_P0_MASK) == entry_sw) {
        m87_deep_sleep_wfi();
    }

    m87_disarm_wake_irqs();

    /* Restore QMK matrix pins: rows OUTPUT HIGH first, then cols INPUT PULL-UP */
    SN_GPIO3->BSET  = M87_MATRIX_ROWS_P3_MASK;
    SN_GPIO3->MODE |= M87_MATRIX_ROWS_P3_MASK;
    SN_GPIO3->CFG  &= ~M87_MATRIX_ROWS_P3_CFG_MASK;

    SN_GPIO2->MODE &= ~M87_MATRIX_COLS_P2_MASK;
    SN_GPIO2->CFG  &= ~M87_MATRIX_COLS_P2_CFG_MASK;
    SN_GPIO1->MODE &= ~M87_MATRIX_COLS_P1_MASK;
    SN_GPIO1->CFG  &= ~M87_MATRIX_COLS_P1_CFG_MASK;

    /* Resume CT16B1 (PWMD1) */
    SN_SYS1->AHBCLKEN |= (1u << 7);
    SN_CT16B1->PWMENB  = saved_pwmenb;
    SN_CT16B1->IC      = 0x01FFFFFFu;
    SN_CT16B1->TC      = 0xFFFFu;
    SN_CT16B1->TMRCTRL = 1u;
    NVIC_ClearPendingIRQ(CT16B1_IRQn);
    nvicEnableVector(SN32_CT16B1_NUMBER, SN32_PWM_CT16B1_IRQ_PRIORITY);

    chSysUnlock();

    m87_refresh_hardware_indicators();
    last_matrix_activity_trigger();
    ch582f_on_wakeup();
}
