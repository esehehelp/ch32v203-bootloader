/*
 * board.h — gameboard pin map and SysTick time base.
 * SPDX-License-Identifier: MIT
 *
 * Pin map (current board; gamegame.kicad_sch in the repo is an older rev):
 *   PB6  I2C1 SCL  (display)
 *   PB7  I2C1 SDA  (display)
 *   Buttons, active low (internal pull-ups):
 *     enter PA1, left PA8, right PA2, up PA10, down PA3
 *   PA9  Buzzer, TIM1_CH2
 *   PA11/PA12 USB D-/D+, PA13/PA14 SWD, PB8 BOOT0
 */
#ifndef BOARD_H
#define BOARD_H

#include "ch32v_regs.h"

/* Buttons (all on GPIOA) */
#define BTN_UP     10
#define BTN_DOWN   3
#define BTN_LEFT   8
#define BTN_RIGHT  2
#define BTN_ENTER  1

#define BUZZ_PIN   9    /* PA9 */

/* Pins the button screen monitors for "which pin went low" */
#define MON_PA_MASK  0x85FFU   /* PA0-PA8, PA10, PA15 (PA9 = buzzer) */
#define MON_PB_MASK  0x003FU   /* PB0-PB5 */

/* SysTick runs at HCLK/8 = 12 MHz, free-running 64-bit up-counter */
static inline void systick_init(void) {
    SysTick->CTLR = 0;
    SysTick->CNTL = 0;
    SysTick->CNTH = 0;
    SysTick->CTLR = STK_CTLR_STE;
}

static inline uint64_t systick_read(void) {
    uint32_t hi, lo, hi2;
    do { hi = SysTick->CNTH; lo = SysTick->CNTL; hi2 = SysTick->CNTH; } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

static inline uint32_t millis(void) {
    return (uint32_t)(systick_read() / STK_TICKS_PER_MS);
}

static inline void delay_ms(uint32_t ms) {
    uint64_t end = systick_read() + (uint64_t)ms * STK_TICKS_PER_MS;
    while (systick_read() < end) {}
}

#endif /* BOARD_H */
