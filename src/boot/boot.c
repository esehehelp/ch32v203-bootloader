/*
 * CH32V203 USB CDC bootloader — lives in the first 8K of flash.
 * SPDX-License-Identifier: MIT
 *
 * Start-up decision (nothing is initialised before it, so the app starts
 * from an untouched, reset-like chip):
 *   - _boot_magic == CH32V_BOOT_MAGIC  (app got a 1200bps touch) → stay
 *   - no valid app (image marker at APP_BASE + 8 missing)        → stay
 *   - BOOT_BUTTON held at reset (optional, see below)            → stay
 *   - RESET pressed twice within BOOT_DOUBLE_TAP_MS              → stay
 *   - otherwise                                                  → jump to app
 *
 * Build options (env:bootloader build_flags):
 *   -DBOOT_DOUBLE_TAP_MS=500   double-tap window; 0 disables it (and the
 *                              start-up delay it adds)
 *   -DBOOT_BUTTON_PORT=GPIOA -DBOOT_BUTTON_PIN=1
 *                              optional recovery button, active low
 *
 * Protocol (binary, over CDC; every request gets one reply):
 *   request : cmd u8 | addr u32 LE | len u16 LE | data[len]
 *   reply   : 'K' payload…  or  'E' code u8
 *   'I'  info      → 'K' "CH32V203BOOT1" app_base u32 flash_end u32 page u16
 *   'E'  erase     addr/len 256-aligned, inside the app area
 *   'W'  write     addr 256-aligned, len ≤ 256 (padded with 0xFF)
 *   'C'  crc32     → 'K' crc u32 (IEEE, same as zlib.crc32)
 *   'R'  read      len ≤ 256 → 'K' data
 *   'G'  go        → 'K', then reset into the app
 */

#include "ch32v_regs.h"
#include "ch32v_cdc.h"
#include <string.h>

#define APP_BASE    0x00002000U
#define APP_MARKER  0x41505031U       /* startup.S vector table entry 1 */
#define DTAP_MAGIC  0xD0B1E7A9U       /* armed during the double-tap window */

#ifndef BOOT_DOUBLE_TAP_MS
#define BOOT_DOUBLE_TAP_MS  500
#endif

/* Flash size in KiB from the electronic signature (64 on C8/K8, 32 on C6/K6…) */
#define FLASH_END   ((uint32_t)*(volatile uint16_t *)ESIG_FLACAP * 1024U)

#define ERR_CMD     1
#define ERR_RANGE   2
#define ERR_VERIFY  3

extern volatile uint32_t _boot_magic;

void NMI_Handler(void)       __attribute__((interrupt));
void HardFault_Handler(void) __attribute__((interrupt));
void NMI_Handler(void)       {}
void HardFault_Handler(void) { PFIC->CFGR = PFIC_KEY3 | PFIC_SYSRST; }

/* ══════════════════════════════════════════════════════════════
 *  Flash (fast 256-byte page mode, same sequence as the BootROM)
 * ══════════════════════════════════════════════════════════════ */
static void flash_wait(void) {
    while (FLASH->STATR & FLASH_STATR_BSY) {}
    FLASH->STATR = FLASH_STATR_EOP;
}

static void flash_unlock(void) {
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    FLASH->MODEKEYR = FLASH_KEY1;
    FLASH->MODEKEYR = FLASH_KEY2;
}

static void flash_lock(void) {
    FLASH->CTLR |= FLASH_CTLR_LOCK | FLASH_CTLR_FLOCK;
}

static void flash_erase_page(uint32_t addr) {
    FLASH->CTLR |= FLASH_CTLR_FTER;
    FLASH->ADDR = FLASH_PHYS_BASE + addr;
    FLASH->CTLR |= FLASH_CTLR_STRT;
    flash_wait();
    FLASH->CTLR &= ~FLASH_CTLR_FTER;
}

static void flash_program_page(uint32_t addr, const uint32_t *words) {
    volatile uint32_t *dst = (volatile uint32_t *)(FLASH_PHYS_BASE + addr);
    FLASH->CTLR |= FLASH_CTLR_FTPG;
    flash_wait();
    while (FLASH->STATR & FLASH_STATR_WRBSY) {}
    for (unsigned i = 0; i < FLASH_FAST_PAGE / 4; i++) {
        dst[i] = words[i];
        while (FLASH->STATR & FLASH_STATR_WRBSY) {}
    }
    FLASH->CTLR |= FLASH_CTLR_PGSTART;
    flash_wait();
    FLASH->CTLR &= ~FLASH_CTLR_FTPG;
}

static uint32_t crc32(uint32_t addr, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    const volatile uint8_t *p = (const volatile uint8_t *)addr;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1));
    }
    return ~crc;
}

/* ══════════════════════════════════════════════════════════════
 *  Protocol
 * ══════════════════════════════════════════════════════════════ */
static uint8_t  frame[7 + FLASH_FAST_PAGE];
static uint16_t frame_len;
static uint32_t page_buf[FLASH_FAST_PAGE / 4];

static void reply_ok(const void *payload, size_t n) {
    uint8_t k = 'K';
    ch32v_cdc_write(&k, 1);
    if (n) ch32v_cdc_write(payload, n);
}

static void reply_err(uint8_t code) {
    uint8_t b[2] = { 'E', code };
    ch32v_cdc_write(b, 2);
}

static int in_app(uint32_t addr, uint32_t len) {
    return addr >= APP_BASE && len <= FLASH_END - APP_BASE && addr + len <= FLASH_END;
}

static void run_app(void);

static void handle(uint8_t cmd, uint32_t addr, uint16_t len, const uint8_t *data) {
    switch (cmd) {
    case 'I': {
        uint8_t info[13 + 10];
        memcpy(info, "CH32V203BOOT1", 13);
        uint32_t a = APP_BASE, e = FLASH_END;
        uint16_t pg = FLASH_FAST_PAGE;
        memcpy(info + 13, &a, 4);
        memcpy(info + 17, &e, 4);
        memcpy(info + 21, &pg, 2);
        reply_ok(info, sizeof(info));
        break;
    }
    case 'E':
        if (!in_app(addr, len ? len : FLASH_FAST_PAGE) || (addr | len) % FLASH_FAST_PAGE) {
            reply_err(ERR_RANGE);
            break;
        }
        /* len is u16, so a full erase is sent as several requests */
        flash_unlock();
        for (uint32_t a = addr; a < addr + len; a += FLASH_FAST_PAGE)
            flash_erase_page(a);
        flash_lock();
        reply_ok(0, 0);
        break;
    case 'W':
        if (len > FLASH_FAST_PAGE || addr % FLASH_FAST_PAGE || !in_app(addr, FLASH_FAST_PAGE)) {
            reply_err(ERR_RANGE);
            break;
        }
        memset(page_buf, 0xFF, sizeof(page_buf));
        memcpy(page_buf, data, len);
        flash_unlock();
        flash_program_page(addr, page_buf);
        flash_lock();
        if (memcmp((const void *)addr, page_buf, FLASH_FAST_PAGE) != 0) reply_err(ERR_VERIFY);
        else reply_ok(0, 0);
        break;
    case 'C': {
        /* 'C' may span the whole flash: len counts 256-byte units if addr has bit 31 */
        uint32_t n = (addr & 0x80000000U) ? (uint32_t)len * FLASH_FAST_PAGE : len;
        addr &= ~0x80000000U;
        if (addr + n > FLASH_END) { reply_err(ERR_RANGE); break; }
        uint32_t c = crc32(addr, n);
        reply_ok(&c, 4);
        break;
    }
    case 'R':
        if (len > FLASH_FAST_PAGE || addr + len > FLASH_END) { reply_err(ERR_RANGE); break; }
        reply_ok((const void *)addr, len);
        break;
    case 'G':
        reply_ok(0, 0);
        ch32v_cdc_flush();
        for (volatile uint32_t i = 0; i < 2000000; i++) {}   /* let the reply go out */
        run_app();
        break;
    default:
        reply_err(ERR_CMD);
        break;
    }
}

static void protocol_poll(void) {
    static uint32_t idle;
    int c;
    while ((c = ch32v_cdc_read()) >= 0) {
        frame[frame_len++] = (uint8_t)c;
        idle = 0;
        if (frame_len >= 7) {
            uint16_t len = (uint16_t)(frame[5] | (frame[6] << 8));
            uint8_t cmd = frame[0];
            /* only 'W' carries data */
            uint16_t need = (cmd == 'W') ? len : 0;
            if (need > FLASH_FAST_PAGE) { frame_len = 0; reply_err(ERR_RANGE); continue; }
            if (frame_len == 7 + need) {
                uint32_t addr = (uint32_t)frame[1] | ((uint32_t)frame[2] << 8)
                              | ((uint32_t)frame[3] << 16) | ((uint32_t)frame[4] << 24);
                frame_len = 0;
                handle(cmd, addr, len, frame + 7);
            }
        }
    }
    /* Drop a half-received frame after a long silence */
    if (frame_len && ++idle > 3000000) { frame_len = 0; idle = 0; }
}

/* ══════════════════════════════════════════════════════════════
 *  Start-up
 * ══════════════════════════════════════════════════════════════ */

#define GO_MAGIC  0x60A99A60U          /* set by 'G': start the app without waiting */

/* Reset the chip and start the app from a freshly reset state instead of
 * handing over a running USB stack. */
static void run_app(void) {
    __disable_irq();
    EXTEN->CTR &= ~EXTEN_USBD_PU_EN;
    USBD_CNTR = USBD_CNTR_FRES | USBD_CNTR_PDWN;
    for (volatile uint32_t i = 0; i < 2000000; i++) {}    /* host sees detach */
    _boot_magic = GO_MAGIC;
    PFIC->CFGR = PFIC_KEY3 | PFIC_SYSRST;
    for (;;) {}
}

#ifdef BOOT_BUTTON_PIN
static int button_held(void) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB;
    gpio_cfg(BOOT_BUTTON_PORT, BOOT_BUTTON_PIN, GPIO_CFG_IN_PUPD);
    BOOT_BUTTON_PORT->BSHR = 1U << BOOT_BUTTON_PIN;
    for (volatile int i = 0; i < 2000; i++) {}             /* pull-up settle */
    int held = !(BOOT_BUTTON_PORT->INDR & (1U << BOOT_BUTTON_PIN));
    /* back to reset state for the app */
    gpio_cfg(BOOT_BUTTON_PORT, BOOT_BUTTON_PIN, GPIO_CFG_IN_FLOAT);
    BOOT_BUTTON_PORT->BCR = 1U << BOOT_BUTTON_PIN;
    RCC->APB2PCENR &= ~(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB);
    return held;
}
#else
static int button_held(void) { return 0; }
#endif

/* Arm a RAM flag for BOOT_DOUBLE_TAP_MS: a second reset inside that window
 * finds it still set. RAM survives NRST and software resets. */
static int double_tapped(uint32_t magic) {
    if (BOOT_DOUBLE_TAP_MS == 0) return 0;
    if (magic == DTAP_MAGIC) return 1;
    _boot_magic = DTAP_MAGIC;
    /* SysTick at reset clock: HSI 8 MHz / 8 = 1 tick per µs */
    SysTick->CTLR = 0;
    SysTick->CNTL = 0;
    SysTick->CNTH = 0;
    SysTick->CTLR = STK_CTLR_STE;
    while (SysTick->CNTL < BOOT_DOUBLE_TAP_MS * 1000U) {}
    SysTick->CTLR = 0;
    SysTick->CNTL = 0;
    _boot_magic = 0;
    return 0;
}

int main(void) {
    uint32_t magic = _boot_magic;
    _boot_magic = 0;
    /* _start (4 B) then the vector table: entry 1 sits at APP_BASE + 8 */
    int app_ok = *(volatile uint32_t *)(APP_BASE + 8) == APP_MARKER;

    if (app_ok && (magic == GO_MAGIC ||
                   (magic != CH32V_BOOT_MAGIC && !button_held() && !double_tapped(magic)))) {
        __disable_irq();
        __asm volatile("jr %0" :: "r"(APP_BASE));
    }

    ch32v_cdc_config_t cfg = { .magic_baud_enable = 0 };
    ch32v_cdc_init(&cfg);

    for (;;) {
        protocol_poll();
        IWDG->CTLR = 0xAAAA;
    }
}
