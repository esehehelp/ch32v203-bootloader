/*
 * ch32v_cdc.c — USB CDC-ACM library for CH32V203.
 * Port of ch32x-cdc (CH32X035). SPDX-License-Identifier: MIT
 *
 * Single-file implementation: clock, descriptors, ISR, ring buffer, TX, API.
 * Endpoints: EP0 control, EP1 IN interrupt (CDC notify), EP2 OUT bulk (RX),
 *            EP3 IN bulk (TX).
 *
 * Differences from the CH32X035 version:
 *  - Uses the USBD block (the one wired to PA11/PA12 and used by the factory
 *    ISP), which has packet memory and STM32-style EPnR registers instead of
 *    the USBFS DMA engine the X035 has.
 *  - USB clock comes from PLL (96 MHz / 2). No USBPD / CC setup: the board has
 *    discrete 5.1k Rd resistors and the V203 has no USBPD block.
 *  - V203 has no FLASH BOOT_MODE switch, and the factory ISP code is linked to
 *    run from the 0x0 alias (BOOT0 = 1 only), so it cannot be entered from
 *    software. The 1200bps touch instead reboots into our own CDC bootloader
 *    in the first 8K of flash (src/boot), flagged through _boot_magic.
 */

#include "ch32v_regs.h"
#include "ch32v_cdc.h"
#include <string.h>

/* ── Constants ───────────────────────────────────────────────── */
#define EP0_SIZE    64
#define EP_SIZE     64

/* Packet memory layout (byte offsets in USB address space) */
#define PMA_EP0_TX  0x040
#define PMA_EP0_RX  0x080
#define PMA_EP1_TX  0x0C0
#define PMA_EP2_RX  0x100
#define PMA_EP3_TX  0x140
#define PMA_RX_64B  0x8400      /* COUNT_RX: BL_SIZE=1 (32 B blocks), NUM_BLOCK=1 */

/* BTABLE entry fields */
#define BT_ADDR_TX  0
#define BT_COUNT_TX 2
#define BT_ADDR_RX  4
#define BT_COUNT_RX 6

/* USB standard request codes */
#define USB_GET_STATUS          0x00
#define USB_CLEAR_FEATURE       0x01
#define USB_SET_FEATURE         0x03
#define USB_SET_ADDRESS         0x05
#define USB_GET_DESCRIPTOR      0x06
#define USB_GET_CONFIGURATION   0x08
#define USB_SET_CONFIGURATION   0x09
#define USB_GET_INTERFACE       0x0A
#define USB_SET_INTERFACE       0x0B

/* USB descriptor types */
#define USB_DESCR_TYP_DEVICE    0x01
#define USB_DESCR_TYP_CONFIG    0x02
#define USB_DESCR_TYP_STRING    0x03

/* USB request type masks */
#define USB_REQ_TYP_MASK        0x60
#define USB_REQ_TYP_STANDARD    0x00
#define USB_REQ_TYP_CLASS       0x20
#define USB_REQ_RECIP_MASK      0x1F
#define USB_REQ_RECIP_DEVICE    0x00
#define USB_REQ_RECIP_ENDP      0x02
#define USB_REQ_FEAT_ENDP_HALT  0x00

/* CDC class requests */
#define CDC_SET_LINE_CODING     0x20
#define CDC_GET_LINE_CODING     0x21
#define CDC_SET_LINE_CTLSTE     0x22
#define CDC_SEND_BREAK          0x23

/* Endpoint direction */
#define UEP_IN   0x80
#define UEP_OUT  0x00

/* ── USB Setup Packet ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  bRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} USB_SETUP_REQ;

/* ── Descriptors (all const, placed in flash) ────────────────── */
#ifdef CH32V_CDC_BOOTLOADER
#define CDC_PID  CH32V_CDC_PID_BOOT
#else
#define CDC_PID  CH32V_CDC_PID_APP   /* ch32x-cdc uses 0xFE0C */
#endif

static const uint8_t dev_descr[] = {
    0x12, 0x01, 0x00, 0x02, 0x02, 0x00, 0x00, EP0_SIZE,
    CH32V_CDC_VID & 0xFF, CH32V_CDC_VID >> 8,
    CDC_PID & 0xFF, CDC_PID >> 8,
    0x01, 0x00,  /* bcdDevice 0.01 */
    0x01, 0x02, 0x03, 0x01
};

static const uint8_t cfg_descr[] = {
    /* Configuration descriptor */
    0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
    /* Interface 0: CDC (comm) */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    /* CDC Header */
    0x05, 0x24, 0x00, 0x10, 0x01,
    /* CDC Call Management */
    0x05, 0x24, 0x01, 0x00, 0x01,
    /* CDC ACM */
    0x04, 0x24, 0x02, 0x02,
    /* CDC Union */
    0x05, 0x24, 0x06, 0x00, 0x01,
    /* EP1 IN: interrupt, 8 bytes, 255ms */
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0xFF,
    /* Interface 1: CDC Data */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    /* EP2 OUT: bulk, 64 bytes */
    0x07, 0x05, 0x02, 0x02, EP_SIZE, 0x00, 0x00,
    /* EP3 IN: bulk, 64 bytes */
    0x07, 0x05, 0x83, 0x02, EP_SIZE, 0x00, 0x00,
};

static const uint8_t lang_descr[] = { 0x04, 0x03, 0x09, 0x04 };

static const uint8_t manu_descr[] = {
    0x12, 0x03,
    'C',0, 'H',0, '3',0, '2',0, 'V',0, '2',0, '0',0, '3',0
};

#ifdef CH32V_CDC_BOOTLOADER
static const uint8_t prod_descr[] = {
    0x16, 0x03,
    'B',0, 'o',0, 'o',0, 't',0, 'l',0, 'o',0, 'a',0, 'd',0, 'e',0, 'r',0
};
#else
static const uint8_t prod_descr[] = {
    0x16, 0x03,
    'U',0, 'S',0, 'B',0, ' ',0, 'S',0, 'e',0, 'r',0, 'i',0, 'a',0, 'l',0
};
#endif

/* Serial number: filled at runtime from chip UID (96-bit = 24 hex chars) */
static uint8_t serial_descr[50] = {
    0x32, 0x03,
    '0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,
    '0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,
    '0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0
};

/* ── CDC line coding ─────────────────────────────────────────── */
static uint8_t cdc_line_coding[7] = {
    0x00, 0xC2, 0x01, 0x00,  /* 115200 bps LE */
    0x00, 0x00, 0x08          /* 1 stop, no parity, 8 data */
};

/* ── USB state ───────────────────────────────────────────────── */
static USB_SETUP_REQ setup;
static const uint8_t *ep0_ptr;
static uint16_t ep0_left;
static uint8_t  ep0_zlp;            /* short IN needs a trailing ZLP */
static uint8_t  ep0_out_pending;    /* waiting for SET_LINE_CODING data */
static uint8_t  ep0_status[2];      /* GET_STATUS / GET_CONFIGURATION reply */
static uint8_t  pending_addr;
static volatile uint8_t devConfig;

/* ── Reboot state (polled from main loop) ────────────────────── */
static volatile uint8_t  reboot_pending;
static volatile uint8_t  magic_baud_armed;

/* ── RX ring buffer (256B, 8-bit wrap = no modulo) ───────────── */
static volatile uint8_t rx_ring[256];
static volatile uint8_t rx_head, rx_tail;
static volatile uint8_t rx_held;    /* EP2 left at NAK: ring was too full */

/* ── TX state ────────────────────────────────────────────────── */
static volatile uint8_t tx_busy;

/* ── Config ──────────────────────────────────────────────────── */
static int magic_baud_enable = 1;
static uint32_t magic_baud = 1200;
static void (*pre_reboot_hook)(void);

/* ── Weak RX callback ────────────────────────────────────────── */
__attribute__((weak))
void ch32v_cdc_on_rx(const uint8_t *buf, size_t len) {
    (void)buf; (void)len;
}

/* ── Busy-wait (only used where SysTick may not be running) ──── */
static void spin_ms(uint32_t ms) {
    /* ~4 cycles per iteration at 96 MHz */
    for (volatile uint32_t i = 0; i < ms * (HCLK_HZ / 4000); i++) __asm volatile("");
}

/* ── Build serial number from chip UID ───────────────────────── */
static void build_serial(void) {
    static const char hex[] = "0123456789ABCDEF";
    const uint8_t *uid = (const uint8_t *)ESIG_UID_BASE;
    for (int i = 0; i < 12; i++) {
        serial_descr[2 + i*4 + 0] = hex[(uid[i] >> 4) & 0xF];
        serial_descr[2 + i*4 + 1] = 0;
        serial_descr[2 + i*4 + 2] = hex[uid[i] & 0xF];
        serial_descr[2 + i*4 + 3] = 0;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  Packet memory + endpoint register helpers
 * ══════════════════════════════════════════════════════════════ */

/* PMA is 16 bit wide, each halfword occupies a 32-bit slot on the bus */
static volatile uint32_t *pma(uint16_t off) {
    return (volatile uint32_t *)(USBD_PMA_BASE + (uint32_t)off * 2);
}

static volatile uint32_t *btable(uint8_t ep, uint8_t field) {
    return pma((uint16_t)(ep * 8 + field));
}

static void pma_write(uint16_t off, const uint8_t *src, uint16_t n) {
    volatile uint32_t *p = pma(off);
    for (uint16_t i = 0; i < n; i += 2) {
        uint16_t v = src[i];
        if (i + 1 < n) v |= (uint16_t)(src[i + 1] << 8);
        *p++ = v;
    }
}

static void pma_read(uint16_t off, uint8_t *dst, uint16_t n) {
    volatile uint32_t *p = pma(off);
    for (uint16_t i = 0; i < n; i += 2) {
        uint16_t v = (uint16_t)*p++;
        dst[i] = (uint8_t)v;
        if (i + 1 < n) dst[i + 1] = (uint8_t)(v >> 8);
    }
}

/* STAT/DTOG bits are toggle-on-write-1, CTR bits clear-on-write-0. */
static void ep_stat_tx(uint8_t ep, uint32_t stat) {
    uint32_t r = USBD_EPR[ep];
    USBD_EPR[ep] = ((r & (USBD_EP_NT_MASK | USBD_EP_STAT_TX)) ^ (stat << 4))
                 | USBD_EP_CTR_RX | USBD_EP_CTR_TX;
}

static void ep_stat_rx(uint8_t ep, uint32_t stat) {
    uint32_t r = USBD_EPR[ep];
    USBD_EPR[ep] = ((r & (USBD_EP_NT_MASK | USBD_EP_STAT_RX)) ^ (stat << 12))
                 | USBD_EP_CTR_RX | USBD_EP_CTR_TX;
}

static void ep_clr_ctr_rx(uint8_t ep) {
    USBD_EPR[ep] = (USBD_EPR[ep] & USBD_EP_NT_MASK) | USBD_EP_CTR_TX;
}

static void ep_clr_ctr_tx(uint8_t ep) {
    USBD_EPR[ep] = (USBD_EPR[ep] & USBD_EP_NT_MASK) | USBD_EP_CTR_RX;
}

/* Set type/address and drive STAT + DTOG (both directions) to known values */
static void ep_init(uint8_t ep, uint32_t type, uint32_t stat_tx, uint32_t stat_rx) {
    uint32_t r = USBD_EPR[ep];
    uint32_t want = (stat_tx << 4) | (stat_rx << 12);       /* DTOG = 0 */
    USBD_EPR[ep] = type | ep | ((r ^ want) & (USBD_EP_STAT_TX | USBD_EP_DTOG_TX |
                                               USBD_EP_STAT_RX | USBD_EP_DTOG_RX));
}

static uint32_t ep_get_stat_tx(uint8_t ep) { return (USBD_EPR[ep] >> 4) & 3; }
static uint32_t ep_get_stat_rx(uint8_t ep) { return (USBD_EPR[ep] >> 12) & 3; }

/* ── Endpoint init (on bus reset) ────────────────────────────── */
static void endp_init(void) {
    USBD_BTABLE = 0;
    *btable(0, BT_ADDR_TX)  = PMA_EP0_TX;
    *btable(0, BT_COUNT_TX) = 0;
    *btable(0, BT_ADDR_RX)  = PMA_EP0_RX;
    *btable(0, BT_COUNT_RX) = PMA_RX_64B;
    *btable(1, BT_ADDR_TX)  = PMA_EP1_TX;
    *btable(1, BT_COUNT_TX) = 0;
    *btable(2, BT_ADDR_RX)  = PMA_EP2_RX;
    *btable(2, BT_COUNT_RX) = PMA_RX_64B;
    *btable(3, BT_ADDR_TX)  = PMA_EP3_TX;
    *btable(3, BT_COUNT_TX) = 0;

    ep_init(0, USBD_EP_CONTROL,   USBD_STAT_NAK, USBD_STAT_VALID);
    ep_init(1, USBD_EP_INTERRUPT, USBD_STAT_NAK, USBD_STAT_DIS);
    ep_init(2, USBD_EP_BULK,      USBD_STAT_DIS, USBD_STAT_VALID);
    ep_init(3, USBD_EP_BULK,      USBD_STAT_NAK, USBD_STAT_DIS);
    tx_busy = 0;
    rx_held = 0;
}

/* ══════════════════════════════════════════════════════════════
 *  System clock: HSI 8 MHz → PLL ×12 = 96 MHz, USB = 96/2 = 48 MHz
 * ══════════════════════════════════════════════════════════════ */
void ch32v_clock_init(void) {
    RCC->CTLR |= RCC_HSION;
    /* PLL settings may only change while PLL is off and not SYSCLK */
    if ((RCC->CFGR0 & RCC_SWS_MASK) == RCC_SWS_PLL) {
        RCC->CFGR0 &= ~RCC_SW_MASK;
        while (RCC->CFGR0 & RCC_SWS_MASK) {}
    }
    RCC->CTLR &= ~RCC_PLLON;

    EXTEN->CTR |= EXTEN_PLL_HSI_PRE;  /* PLL input = HSI (8 MHz), not HSI/2 */
    RCC->CFGR0 = (RCC->CFGR0 & ~(RCC_HPRE_MASK | RCC_PPRE1_MASK | RCC_PPRE2_MASK |
                                 RCC_PLLSRC_HSE | RCC_PLLMUL_MASK | RCC_USBPRE_MASK))
               | RCC_PPRE1_DIV4 | RCC_PLLMUL_12 | RCC_USBPRE_DIV2;

    RCC->CTLR |= RCC_PLLON;
    while (!(RCC->CTLR & RCC_PLLRDY)) {}
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW_MASK) | RCC_SW_PLL;
    while ((RCC->CFGR0 & RCC_SWS_MASK) != RCC_SWS_PLL) {}
}

/* ══════════════════════════════════════════════════════════════
 *  Reboot to bootloader
 * ══════════════════════════════════════════════════════════════ */
extern volatile uint32_t _boot_magic;   /* linker: last word of RAM */

void ch32v_cdc_reboot_to_bootloader(void) {
    __disable_irq();
    if (pre_reboot_hook) pre_reboot_hook();

    /* Drop off the bus long enough for the host to notice */
    EXTEN->CTR &= ~EXTEN_USBD_PU_EN;
    USBD_CNTR = USBD_CNTR_FRES | USBD_CNTR_PDWN;
    spin_ms(100);

    _boot_magic = CH32V_BOOT_MAGIC;
    RCC->RSTSCKR |= RCC_RSTSCKR_RMVF;
    PFIC->CFGR = PFIC_KEY3 | PFIC_SYSRST;
    for (;;) __asm volatile("nop");
}

/* ══════════════════════════════════════════════════════════════
 *  Control endpoint
 * ══════════════════════════════════════════════════════════════ */

static void ep0_stall(void) {
    ep_stat_tx(0, USBD_STAT_STALL);
    ep_stat_rx(0, USBD_STAT_STALL);
}

static void ep0_send_chunk(void) {
    uint16_t n = (ep0_left > EP0_SIZE) ? EP0_SIZE : ep0_left;
    pma_write(PMA_EP0_TX, ep0_ptr, n);
    *btable(0, BT_COUNT_TX) = n;
    ep0_ptr  += n;
    ep0_left -= n;
    if (n < EP0_SIZE) ep0_zlp = 0;
    ep_stat_tx(0, USBD_STAT_VALID);
}

static void ep0_send_status(void) {
    *btable(0, BT_COUNT_TX) = 0;
    ep0_left = 0;
    ep0_zlp = 0;
    ep_stat_tx(0, USBD_STAT_VALID);
}

static void handle_setup(void) {
    const uint8_t *data = 0;
    uint16_t len = 0;
    uint8_t  err = 0;
    uint8_t  type  = setup.bRequestType & USB_REQ_TYP_MASK;
    uint8_t  recip = setup.bRequestType & USB_REQ_RECIP_MASK;

    ep0_left = 0;
    ep0_zlp = 0;
    ep0_out_pending = 0;

    if (type == USB_REQ_TYP_CLASS) {
        switch (setup.bRequest) {
        case CDC_GET_LINE_CODING:
            data = cdc_line_coding; len = 7; break;
        case CDC_SET_LINE_CODING:
            ep0_out_pending = 1;
            ep_stat_rx(0, USBD_STAT_VALID);
            return;                      /* status sent after the data stage */
        case CDC_SEND_BREAK:
            break;
        case CDC_SET_LINE_CTLSTE:
            /* A touch is a 1200-bps open followed by DTR release.
             * Selecting 1200 in Serial Monitor alone must not reboot. */
            if (magic_baud_armed && !(setup.wValue & 0x0001u)) {
                reboot_pending = 1;
                magic_baud_armed = 0;
            }
            break;
        default: err = 1; break;
        }
    }
    else if (type == USB_REQ_TYP_STANDARD) {
        switch (setup.bRequest) {

        case USB_GET_DESCRIPTOR:
            switch ((uint8_t)(setup.wValue >> 8)) {
            case USB_DESCR_TYP_DEVICE:
                data = dev_descr;  len = sizeof(dev_descr);  break;
            case USB_DESCR_TYP_CONFIG:
                data = cfg_descr;  len = sizeof(cfg_descr);  break;
            case USB_DESCR_TYP_STRING:
                switch ((uint8_t)(setup.wValue & 0xFF)) {
                case 0: data = lang_descr;   len = sizeof(lang_descr);   break;
                case 1: data = manu_descr;   len = sizeof(manu_descr);   break;
                case 2: data = prod_descr;   len = sizeof(prod_descr);   break;
                case 3: data = serial_descr; len = sizeof(serial_descr); break;
                default: err = 1; break;
                }
                break;
            default: err = 1; break;
            }
            break;

        case USB_SET_ADDRESS:
            pending_addr = (uint8_t)(setup.wValue & 0x7F);   /* applied after status */
            break;

        case USB_GET_CONFIGURATION:
            ep0_status[0] = devConfig;
            data = ep0_status; len = 1;
            break;

        case USB_SET_CONFIGURATION:
            devConfig = (uint8_t)setup.wValue;
            break;

        case USB_CLEAR_FEATURE:
            if (recip == USB_REQ_RECIP_ENDP) {
                switch ((uint8_t)setup.wIndex) {
                case (UEP_IN|1):  ep_init(1, USBD_EP_INTERRUPT, USBD_STAT_NAK, USBD_STAT_DIS); break;
                case (UEP_OUT|2): ep_init(2, USBD_EP_BULK, USBD_STAT_DIS, USBD_STAT_VALID); break;
                case (UEP_IN|3):  ep_init(3, USBD_EP_BULK, USBD_STAT_NAK, USBD_STAT_DIS); tx_busy = 0; break;
                default: err = 1; break;
                }
            } else {
                err = recip != USB_REQ_RECIP_DEVICE;
            }
            break;

        case USB_SET_FEATURE:
            if (recip == USB_REQ_RECIP_ENDP && setup.wValue == USB_REQ_FEAT_ENDP_HALT) {
                switch ((uint8_t)setup.wIndex) {
                case (UEP_IN|1):  ep_stat_tx(1, USBD_STAT_STALL); break;
                case (UEP_OUT|2): ep_stat_rx(2, USBD_STAT_STALL); break;
                case (UEP_IN|3):  ep_stat_tx(3, USBD_STAT_STALL); break;
                default: err = 1; break;
                }
            } else { err = 1; }
            break;

        case USB_GET_STATUS:
            ep0_status[0] = 0; ep0_status[1] = 0;
            if (recip == USB_REQ_RECIP_ENDP) {
                switch ((uint8_t)setup.wIndex) {
                case (UEP_IN|1):  ep0_status[0] = ep_get_stat_tx(1) == USBD_STAT_STALL; break;
                case (UEP_OUT|2): ep0_status[0] = ep_get_stat_rx(2) == USBD_STAT_STALL; break;
                case (UEP_IN|3):  ep0_status[0] = ep_get_stat_tx(3) == USBD_STAT_STALL; break;
                case 0: case 0x80: break;
                default: err = 1; break;
                }
            }
            data = ep0_status; len = 2;
            break;

        case USB_GET_INTERFACE:
            ep0_status[0] = 0;
            data = ep0_status; len = 1;
            break;

        case USB_SET_INTERFACE:
            break;

        default: err = 1; break;
        }
    }
    else { err = 1; }

    if (err) { ep0_stall(); return; }

    if (setup.bRequestType & 0x80) {     /* IN data stage, then OUT status */
        if (len > setup.wLength) len = setup.wLength;
        ep0_ptr  = data;
        ep0_left = len;
        ep0_zlp  = (len < setup.wLength) && (len % EP0_SIZE == 0);
        ep0_send_chunk();
        ep_stat_rx(0, USBD_STAT_VALID);
    } else {                             /* no data: IN status */
        ep0_send_status();
    }
}

static void ep0_rx(uint16_t epr) {
    uint16_t n = (uint16_t)(*btable(0, BT_COUNT_RX) & 0x3FF);

    if (epr & USBD_EP_SETUP) {
        uint8_t b[8] = {0};
        pma_read(PMA_EP0_RX, b, n < 8 ? n : 8);
        ep_clr_ctr_rx(0);
        memcpy(&setup, b, sizeof(setup));
        handle_setup();
        return;
    }

    ep_clr_ctr_rx(0);
    if (ep0_out_pending) {               /* SET_LINE_CODING data stage */
        ep0_out_pending = 0;
        uint8_t b[8] = {0};
        pma_read(PMA_EP0_RX, b, n < 7 ? n : 7);
        memcpy(cdc_line_coding, b, 7);
        if (magic_baud_enable) {
            uint32_t baud = (uint32_t)cdc_line_coding[0]
                          | ((uint32_t)cdc_line_coding[1] << 8)
                          | ((uint32_t)cdc_line_coding[2] << 16)
                          | ((uint32_t)cdc_line_coding[3] << 24);
            magic_baud_armed = (baud == magic_baud);
        }
        ep0_send_status();
    }
    /* else: status stage of an IN transfer — nothing to do */
    ep_stat_rx(0, USBD_STAT_VALID);
}

static void ep0_tx(void) {
    ep_clr_ctr_tx(0);
    if (pending_addr) {
        USBD_DADDR = USBD_DADDR_EF | pending_addr;
        pending_addr = 0;
    }
    if (ep0_left || ep0_zlp)
        ep0_send_chunk();
}

/* ══════════════════════════════════════════════════════════════
 *  Bulk endpoints
 * ══════════════════════════════════════════════════════════════ */

static int rx_room(void) {
    return 255 - (uint8_t)(rx_head - rx_tail);
}

static void ep2_rx(void) {
    uint8_t buf[EP_SIZE];
    uint16_t n = (uint16_t)(*btable(2, BT_COUNT_RX) & 0x3FF);
    if (n > EP_SIZE) n = EP_SIZE;
    pma_read(PMA_EP2_RX, buf, n);
    ep_clr_ctr_rx(2);

    for (uint16_t i = 0; i < n; i++) {
        uint8_t next = (uint8_t)(rx_head + 1);
        if (next != rx_tail) {
            rx_ring[rx_head] = buf[i];
            rx_head = next;
        }
    }
    if (n > 0)
        ch32v_cdc_on_rx(buf, n);

    /* Only accept the next packet if it is guaranteed to fit */
    if (rx_room() >= EP_SIZE) ep_stat_rx(2, USBD_STAT_VALID);
    else                      rx_held = 1;
}

static void rx_resume(void) {
    if (rx_held && rx_room() >= EP_SIZE) {
        rx_held = 0;
        ep_stat_rx(2, USBD_STAT_VALID);
    }
}

/* ══════════════════════════════════════════════════════════════
 *  USBD IRQ Handler
 * ══════════════════════════════════════════════════════════════ */
void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((interrupt));

void USB_LP_CAN1_RX0_IRQHandler(void) {
    uint32_t istr;

    while ((istr = USBD_ISTR) & USBD_ISTR_CTR) {
        uint8_t  ep  = (uint8_t)(istr & USBD_ISTR_EP_ID);
        uint16_t epr = (uint16_t)USBD_EPR[ep];

        if (ep == 0) {
            if (epr & USBD_EP_CTR_TX) ep0_tx();
            if (epr & USBD_EP_CTR_RX) ep0_rx(epr);
        } else {
            if (epr & USBD_EP_CTR_RX) {
                if (ep == 2) ep2_rx();
                else         ep_clr_ctr_rx(ep);
            }
            if (epr & USBD_EP_CTR_TX) {
                ep_clr_ctr_tx(ep);
                if (ep == 3) tx_busy = 0;
            }
        }
    }

    if (istr & USBD_ISTR_RESET) {
        USBD_ISTR = (uint16_t)~USBD_ISTR_RESET;
        devConfig = 0;
        pending_addr = 0;
        ep0_left = 0;
        ep0_zlp = 0;
        ep0_out_pending = 0;
        endp_init();
        USBD_DADDR = USBD_DADDR_EF;
        reboot_pending = 0;
        magic_baud_armed = 0;
        cdc_line_coding[0] = 0x00; cdc_line_coding[1] = 0xC2;
        cdc_line_coding[2] = 0x01; cdc_line_coding[3] = 0x00;
        cdc_line_coding[4] = 0x00; cdc_line_coding[5] = 0x00;
        cdc_line_coding[6] = 0x08;
    }
    if (istr & USBD_ISTR_SUSP) USBD_ISTR = (uint16_t)~USBD_ISTR_SUSP;
    if (istr & USBD_ISTR_WKUP) USBD_ISTR = (uint16_t)~USBD_ISTR_WKUP;
}

/* ══════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════ */

void ch32v_cdc_init(const ch32v_cdc_config_t *cfg) {
    if (cfg) {
        if (cfg->magic_baud)
            magic_baud = cfg->magic_baud;
        magic_baud_enable = cfg->magic_baud_enable;
        pre_reboot_hook = cfg->pre_reboot;
    }

    if ((RCC->CFGR0 & RCC_SWS_MASK) != RCC_SWS_PLL)
        ch32v_clock_init();

    build_serial();

    /* PA11 (D-) / PA12 (D+): floating inputs, the USB PHY takes them over */
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA;
    GPIOA->BCR = (1U << 11) | (1U << 12);
    gpio_cfg(GPIOA, 11, GPIO_CFG_IN_FLOAT);
    gpio_cfg(GPIOA, 12, GPIO_CFG_IN_FLOAT);

    RCC->APB1PCENR |= RCC_APB1Periph_USBD;
    RCC->APB1PRSTR |= RCC_APB1Periph_USBD;
    RCC->APB1PRSTR &= ~RCC_APB1Periph_USBD;

    /* Power up the transceiver, hold reset briefly, then release */
    USBD_CNTR = USBD_CNTR_FRES;
    spin_ms(1);
    USBD_CNTR = 0;
    USBD_ISTR = 0;
    endp_init();
    USBD_DADDR = USBD_DADDR_EF;
    USBD_CNTR = USBD_CNTR_CTRM | USBD_CNTR_RESETM | USBD_CNTR_SUSPM | USBD_CNTR_WKUPM;
    NVIC_EnableIRQ(USBD_IRQn);

    /* Internal 1.5k D+ pull-up: announce ourselves to the host */
    EXTEN->CTR |= EXTEN_USBD_PU_EN;
}

int ch32v_cdc_connected(void) {
    return devConfig != 0;
}

int ch32v_cdc_available(void) {
    return (uint8_t)(rx_head - rx_tail);
}

int ch32v_cdc_peek(void) {
    if (rx_head == rx_tail) return -1;
    return rx_ring[rx_tail];
}

int ch32v_cdc_read(void) {
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx_ring[rx_tail];
    rx_tail = (uint8_t)(rx_tail + 1);
    rx_resume();
    return c;
}

size_t ch32v_cdc_read_buf(uint8_t *buf, size_t max) {
    size_t n = 0;
    while (n < max && rx_head != rx_tail) {
        buf[n++] = rx_ring[rx_tail];
        rx_tail = (uint8_t)(rx_tail + 1);
    }
    rx_resume();
    return n;
}

size_t ch32v_cdc_write(const uint8_t *buf, size_t len) {
    if (!devConfig) return 0;
    size_t total = 0;
    while (total < len) {
        uint32_t timeout = 2000000;
        while (tx_busy && --timeout) __asm volatile("");
        if (tx_busy) return total;
        size_t chunk = len - total;
        if (chunk > EP_SIZE) chunk = EP_SIZE;
        pma_write(PMA_EP3_TX, buf + total, (uint16_t)chunk);
        *btable(3, BT_COUNT_TX) = (uint16_t)chunk;
        tx_busy = 1;
        ep_stat_tx(3, USBD_STAT_VALID);
        total += chunk;
    }
    return total;
}

void ch32v_cdc_flush(void) {
    uint32_t timeout = 2000000;
    while (tx_busy && --timeout) __asm volatile("");
}

size_t ch32v_cdc_print(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return ch32v_cdc_write((const uint8_t *)s, len);
}

void ch32v_cdc_poll(void) {
    if (reboot_pending) {
        /* Wait ~80ms for host to process SET_LINE_CODING response */
        spin_ms(80);
        ch32v_cdc_reboot_to_bootloader();
    }
}
