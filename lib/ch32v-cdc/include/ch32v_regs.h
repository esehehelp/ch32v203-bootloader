/*
 * ch32v_regs.h — Minimal CH32V203 (CH32V20x_D6) register definitions (MIT)
 *
 * Written from the CH32V20x Technical Reference Manual.
 * Covers: RCC, EXTEN, GPIO, AFIO, FLASH, PFIC, SysTick, USBFS (OTG_FS device),
 *         I2C, IWDG.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CH32V_REGS_H
#define CH32V_REGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Compiler helpers ─────────────────────────────────────────── */
#define __IO  volatile
#define __I   volatile const
#define __O   volatile

/* ── Peripheral base addresses ────────────────────────────────── */
#define PERIPH_BASE       0x40000000U
#define APB1PERIPH_BASE   (PERIPH_BASE)
#define APB2PERIPH_BASE   (PERIPH_BASE + 0x10000U)
#define AHBPERIPH_BASE    (PERIPH_BASE + 0x20000U)

/* ── RCC ──────────────────────────────────────────────────────── */
typedef struct {
    __IO uint32_t CTLR;          /* 0x00 Clock control */
    __IO uint32_t CFGR0;         /* 0x04 Clock configuration */
    __IO uint32_t INTR;          /* 0x08 Clock interrupt */
    __IO uint32_t APB2PRSTR;     /* 0x0C APB2 peripheral reset */
    __IO uint32_t APB1PRSTR;     /* 0x10 APB1 peripheral reset */
    __IO uint32_t AHBPCENR;      /* 0x14 AHB peripheral clock enable */
    __IO uint32_t APB2PCENR;     /* 0x18 APB2 peripheral clock enable */
    __IO uint32_t APB1PCENR;     /* 0x1C APB1 peripheral clock enable */
    __IO uint32_t BDCTLR;        /* 0x20 Backup domain control */
    __IO uint32_t RSTSCKR;       /* 0x24 Reset status / clock */
    __IO uint32_t AHBRSTR;       /* 0x28 AHB peripheral reset */
    __IO uint32_t CFGR2;         /* 0x2C Clock configuration 2 */
} RCC_TypeDef;

#define RCC_BASE          (AHBPERIPH_BASE + 0x1000U)
#define RCC               ((RCC_TypeDef *)RCC_BASE)

/* RCC_CTLR bits */
#define RCC_HSION             (1U << 0)
#define RCC_PLLON             (1U << 24)
#define RCC_PLLRDY            (1U << 25)

/* RCC_CFGR0 fields */
#define RCC_SW_MASK           0x03U
#define RCC_SW_HSI            0x00U
#define RCC_SW_PLL            0x02U
#define RCC_SWS_MASK          0x0CU
#define RCC_SWS_PLL           0x08U
#define RCC_HPRE_MASK         (0xFU << 4)
#define RCC_PPRE1_MASK        (0x7U << 8)
#define RCC_PPRE1_DIV4        (0x5U << 8)
#define RCC_PPRE2_MASK        (0x7U << 11)
#define RCC_PLLSRC_HSE        (1U << 16)   /* 0 = HSI (or HSI/2, see EXTEN) */
#define RCC_PLLMUL_MASK       (0xFU << 18)
#define RCC_PLLMUL_12         (0xAU << 18)
#define RCC_USBPRE_MASK       (0x3U << 22)
#define RCC_USBPRE_DIV2       (0x1U << 22) /* 96 MHz PLL → 48 MHz USB */

/* RCC_AHBPCENR bits */
#define RCC_AHBPeriph_USBFS   (1U << 12)

/* RCC_APB2PCENR bits */
#define RCC_APB2Periph_AFIO   (1U << 0)
#define RCC_APB2Periph_GPIOA  (1U << 2)
#define RCC_APB2Periph_GPIOB  (1U << 3)

/* RCC_APB1PCENR / APB1PRSTR bits */
#define RCC_APB1Periph_I2C1   (1U << 21)

/* RCC_RSTSCKR bits */
#define RCC_RSTSCKR_RMVF      (1U << 24)  /* Remove reset flags */

/* ── EXTEN (extended configuration) ───────────────────────────── */
typedef struct {
    __IO uint32_t CTR;
} EXTEN_TypeDef;

#define EXTEN_BASE        (AHBPERIPH_BASE + 0x3800U)
#define EXTEN             ((EXTEN_TypeDef *)EXTEN_BASE)

#define EXTEN_USBD_PU_EN      (1U << 1)
#define EXTEN_PLL_HSI_PRE     (1U << 4)   /* 1 = PLL input is HSI, not HSI/2 */

/* ── GPIO ─────────────────────────────────────────────────────── */
typedef struct {
    __IO uint32_t CFGLR;   /* 0x00 Port configuration low  (pins 0-7) */
    __IO uint32_t CFGHR;   /* 0x04 Port configuration high (pins 8-15) */
    __IO uint32_t INDR;    /* 0x08 Port input data */
    __IO uint32_t OUTDR;   /* 0x0C Port output data */
    __IO uint32_t BSHR;    /* 0x10 Port bit set/reset (high=reset, low=set) */
    __IO uint32_t BCR;     /* 0x14 Port bit reset */
    __IO uint32_t LCKR;    /* 0x18 Port lock */
} GPIO_TypeDef;

#define GPIOA_BASE  (APB2PERIPH_BASE + 0x0800U)
#define GPIOB_BASE  (APB2PERIPH_BASE + 0x0C00U)
#define GPIOA       ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB       ((GPIO_TypeDef *)GPIOB_BASE)

/* 4-bit pin configs (CNF[1:0] << 2 | MODE[1:0]) */
#define GPIO_CFG_IN_FLOAT     0x4U
#define GPIO_CFG_IN_PUPD      0x8U
#define GPIO_CFG_OUT_PP_50M   0x3U
#define GPIO_CFG_OUT_OD_50M   0x7U
#define GPIO_CFG_AF_OD_50M    0xFU

static inline void gpio_cfg(GPIO_TypeDef *port, unsigned pin, uint32_t cfg) {
    volatile uint32_t *r = (pin < 8) ? &port->CFGLR : &port->CFGHR;
    unsigned sh = (pin & 7) * 4;
    *r = (*r & ~(0xFU << sh)) | (cfg << sh);
}

/* ── FLASH ────────────────────────────────────────────────────── */
typedef struct {
    __IO uint32_t ACTLR;          /* 0x00 Access control */
    __IO uint32_t KEYR;           /* 0x04 Key register */
    __IO uint32_t OBKEYR;         /* 0x08 Option byte key */
    __IO uint32_t STATR;          /* 0x0C Status register */
    __IO uint32_t CTLR;           /* 0x10 Control register */
    __IO uint32_t ADDR;           /* 0x14 Address register */
    uint32_t      RESERVED;       /* 0x18 */
    __IO uint32_t OBR;            /* 0x1C Option byte register */
    __IO uint32_t WPR;            /* 0x20 Write protection */
    __IO uint32_t MODEKEYR;       /* 0x24 Fast mode key register */
} FLASH_TypeDef;

#define FLASH_R_BASE  (AHBPERIPH_BASE + 0x2000U)
#define FLASH         ((FLASH_TypeDef *)FLASH_R_BASE)

#define FLASH_KEY1           0x45670123U
#define FLASH_KEY2           0xCDEF89ABU
#define FLASH_PHYS_BASE      0x08000000U   /* erase/program addresses */
#define FLASH_FAST_PAGE      256U

/* STATR */
#define FLASH_STATR_BSY      (1U << 0)
#define FLASH_STATR_WRBSY    (1U << 1)
#define FLASH_STATR_EOP      (1U << 5)
/* CTLR */
#define FLASH_CTLR_STRT      (1U << 6)
#define FLASH_CTLR_LOCK      (1U << 7)
#define FLASH_CTLR_FLOCK     (1U << 15)
#define FLASH_CTLR_FTPG      (1U << 16)   /* fast page program (256 B) */
#define FLASH_CTLR_FTER      (1U << 17)   /* fast page erase (256 B) */
#define FLASH_CTLR_PGSTART   (1U << 21)

/* ── PFIC (Platform-level Fast Interrupt Controller) ──────────── */
typedef struct {
    __I  uint32_t ISR[8];       /* 0x000 Interrupt status */
    __I  uint32_t IPR[8];       /* 0x020 Interrupt pending */
    __IO uint32_t ITHRESDR;     /* 0x040 Interrupt threshold */
    __IO uint32_t RESERVED;     /* 0x044 */
    __IO uint32_t CFGR;         /* 0x048 Configuration (system reset, etc.) */
    __I  uint32_t GISR;         /* 0x04C Global interrupt status */
    __IO uint8_t  VTFIDR[4];    /* 0x050 VTF interrupt ID */
    uint8_t       RESERVED0[12];
    __IO uint32_t VTFADDR[4];   /* 0x060 VTF address */
    uint8_t       RESERVED1[0x90];
    __O  uint32_t IENR[8];      /* 0x100 Interrupt enable set */
    uint8_t       RESERVED2[0x60];
    __O  uint32_t IRER[8];      /* 0x180 Interrupt enable reset */
    uint8_t       RESERVED3[0x60];
    __O  uint32_t IPSR[8];      /* 0x200 Interrupt pending set */
    uint8_t       RESERVED4[0x60];
    __O  uint32_t IPRR[8];      /* 0x280 Interrupt pending reset */
    uint8_t       RESERVED5[0x60];
    __IO uint32_t IACTR[8];     /* 0x300 Interrupt active */
    uint8_t       RESERVED6[0xE0];
    __IO uint8_t  IPRIOR[256];  /* 0x400 Interrupt priority */
    uint8_t       RESERVED7[0x810];
    __IO uint32_t SCTLR;        /* 0xD10 System control */
} PFIC_Type;

#define PFIC  ((PFIC_Type *)0xE000E000U)
#define NVIC  PFIC

#define PFIC_KEY3       0xBEEF0000U
#define PFIC_SYSRST     (1U << 7)

#define USBFS_IRQn      59

static inline void NVIC_EnableIRQ(uint32_t irq) {
    PFIC->IENR[irq >> 5] = 1U << (irq & 0x1F);
}
static inline void NVIC_DisableIRQ(uint32_t irq) {
    PFIC->IRER[irq >> 5] = 1U << (irq & 0x1F);
    __asm volatile("fence.i");
}

/* ── SysTick (QingKe V4B, 64-bit up-counter) ──────────────────── */
typedef struct {
    __IO uint32_t CTLR;   /* 0x00 */
    __IO uint32_t SR;     /* 0x04 */
    __IO uint32_t CNTL;   /* 0x08 */
    __IO uint32_t CNTH;   /* 0x0C */
    __IO uint32_t CMPLR;  /* 0x10 */
    __IO uint32_t CMPHR;  /* 0x14 */
} SysTick_Type;

#define SysTick  ((SysTick_Type *)0xE000F000U)

#define STK_CTLR_STE    (1U << 0)   /* counter enable */
#define STK_CTLR_STCLK  (1U << 2)   /* 0 = HCLK/8, 1 = HCLK */

/* ── USBFS (OTG_FS, device mode) ──────────────────────────────── */
typedef struct {
    __IO uint8_t  BASE_CTRL;     /* 0x00 */
    __IO uint8_t  UDEV_CTRL;     /* 0x01 */
    __IO uint8_t  INT_EN;        /* 0x02 */
    __IO uint8_t  DEV_ADDR;      /* 0x03 */
    uint8_t       RESERVED0;     /* 0x04 */
    __IO uint8_t  MIS_ST;        /* 0x05 */
    __IO uint8_t  INT_FG;        /* 0x06 */
    __IO uint8_t  INT_ST;        /* 0x07 */
    __IO uint32_t RX_LEN;        /* 0x08 */
    __IO uint8_t  UEP4_1_MOD;    /* 0x0C */
    __IO uint8_t  UEP2_3_MOD;    /* 0x0D */
    __IO uint8_t  UEP5_6_MOD;    /* 0x0E */
    __IO uint8_t  UEP7_MOD;      /* 0x0F */
    __IO uint32_t UEP0_DMA;      /* 0x10 */
    __IO uint32_t UEP1_DMA;      /* 0x14 */
    __IO uint32_t UEP2_DMA;      /* 0x18 */
    __IO uint32_t UEP3_DMA;      /* 0x1C */
    __IO uint32_t UEP4_DMA;      /* 0x20 */
    __IO uint32_t UEP5_DMA;      /* 0x24 */
    __IO uint32_t UEP6_DMA;      /* 0x28 */
    __IO uint32_t UEP7_DMA;      /* 0x2C */
    __IO uint16_t UEP0_TX_LEN;   /* 0x30 */
    __IO uint8_t  UEP0_TX_CTRL;  /* 0x32 */
    __IO uint8_t  UEP0_RX_CTRL;  /* 0x33 */
    __IO uint16_t UEP1_TX_LEN;   /* 0x34 */
    __IO uint8_t  UEP1_TX_CTRL;  /* 0x36 */
    __IO uint8_t  UEP1_RX_CTRL;  /* 0x37 */
    __IO uint16_t UEP2_TX_LEN;   /* 0x38 */
    __IO uint8_t  UEP2_TX_CTRL;  /* 0x3A */
    __IO uint8_t  UEP2_RX_CTRL;  /* 0x3B */
    __IO uint16_t UEP3_TX_LEN;   /* 0x3C */
    __IO uint8_t  UEP3_TX_CTRL;  /* 0x3E */
    __IO uint8_t  UEP3_RX_CTRL;  /* 0x3F */
} USBFSD_TypeDef;

#define USBFS_BASE  0x50000000U
#define USBFSD      ((USBFSD_TypeDef *)USBFS_BASE)

/* BASE_CTRL bits */
#define USBFS_UC_DMA_EN       0x01
#define USBFS_UC_CLR_ALL      0x02
#define USBFS_UC_RESET_SIE    0x04
#define USBFS_UC_INT_BUSY     0x08
#define USBFS_UC_DEV_PU_EN    0x20

/* DEV_ADDR bits */
#define USBFS_UDA_GP_BIT      0x80

/* UDEV_CTRL bits */
#define USBFS_UD_PORT_EN      0x01
#define USBFS_UD_PD_DIS       0x80

/* INT_EN bits */
#define USBFS_UIE_BUS_RST     0x01
#define USBFS_UIE_TRANSFER    0x02
#define USBFS_UIE_SUSPEND     0x04

/* INT_FG bits */
#define USBFS_UIF_BUS_RST     0x01
#define USBFS_UIF_TRANSFER    0x02
#define USBFS_UIF_SUSPEND     0x04

/* INT_ST bits */
#define USBFS_UIS_ENDP_MASK   0x0F
#define USBFS_UIS_TOKEN_MASK  0x30
#define USBFS_UIS_TOKEN_OUT   0x00
#define USBFS_UIS_TOKEN_SOF   0x10
#define USBFS_UIS_TOKEN_IN    0x20
#define USBFS_UIS_TOKEN_SETUP 0x30
#define USBFS_UIS_TOG_OK      0x40

/* UEP_MOD bits (UEP4_1_MOD) */
#define USBFS_UEP1_TX_EN      0x40
#define USBFS_UEP1_RX_EN      0x80
/* UEP_MOD bits (UEP2_3_MOD) */
#define USBFS_UEP2_TX_EN      0x04
#define USBFS_UEP2_RX_EN      0x08
#define USBFS_UEP3_TX_EN      0x40
#define USBFS_UEP3_RX_EN      0x80

/* UEPn_TX_CTRL / UEPn_RX_CTRL bits (same layout for both directions) */
#define USBFS_UEP_RES_ACK     0x00
#define USBFS_UEP_RES_NONE    0x01
#define USBFS_UEP_RES_NAK     0x02
#define USBFS_UEP_RES_STALL   0x03
#define USBFS_UEP_RES_MASK    0x03
#define USBFS_UEP_TOG         0x04
#define USBFS_UEP_AUTO_TOG    0x08

/* ── USBD (full-speed device, PA11/PA12, packet memory) ───────── */
#define USBD_BASE         (APB1PERIPH_BASE + 0x5C00U)
#define USBD_PMA_BASE     (APB1PERIPH_BASE + 0x6000U)   /* 512 B, 16 bit per 32 bit word */
#define USBD_EPR          ((volatile uint32_t *)USBD_BASE)          /* EPR[0..7] */
#define USBD_CNTR         (*(volatile uint32_t *)(USBD_BASE + 0x40U))
#define USBD_ISTR         (*(volatile uint32_t *)(USBD_BASE + 0x44U))
#define USBD_FNR          (*(volatile uint32_t *)(USBD_BASE + 0x48U))
#define USBD_DADDR        (*(volatile uint32_t *)(USBD_BASE + 0x4CU))
#define USBD_BTABLE       (*(volatile uint32_t *)(USBD_BASE + 0x50U))

#define USBD_IRQn         36              /* USB_LP_CAN1_RX0 */
#define RCC_APB1Periph_USBD  (1U << 23)

/* CNTR / ISTR bits */
#define USBD_CNTR_FRES    (1U << 0)
#define USBD_CNTR_PDWN    (1U << 1)
#define USBD_CNTR_RESETM  (1U << 10)
#define USBD_CNTR_SUSPM   (1U << 11)
#define USBD_CNTR_WKUPM   (1U << 12)
#define USBD_CNTR_CTRM    (1U << 15)
#define USBD_ISTR_EP_ID   0x000FU
#define USBD_ISTR_RESET   (1U << 10)
#define USBD_ISTR_SUSP    (1U << 11)
#define USBD_ISTR_WKUP    (1U << 12)
#define USBD_ISTR_CTR     (1U << 15)
#define USBD_DADDR_EF     (1U << 7)

/* EPnR bits */
#define USBD_EP_EA        0x000FU
#define USBD_EP_STAT_TX   0x0030U       /* toggle */
#define USBD_EP_DTOG_TX   0x0040U       /* toggle */
#define USBD_EP_CTR_TX    0x0080U       /* write 0 to clear */
#define USBD_EP_KIND      0x0100U
#define USBD_EP_TYPE      0x0600U
#define USBD_EP_SETUP     0x0800U
#define USBD_EP_STAT_RX   0x3000U       /* toggle */
#define USBD_EP_DTOG_RX   0x4000U       /* toggle */
#define USBD_EP_CTR_RX    0x8000U       /* write 0 to clear */
#define USBD_EP_NT_MASK   (USBD_EP_EA | USBD_EP_KIND | USBD_EP_TYPE | USBD_EP_SETUP)

#define USBD_EP_BULK      0x0000U
#define USBD_EP_CONTROL   0x0200U
#define USBD_EP_INTERRUPT 0x0600U

#define USBD_STAT_DIS     0U
#define USBD_STAT_STALL   1U
#define USBD_STAT_NAK     2U
#define USBD_STAT_VALID   3U

/* ── I2C ──────────────────────────────────────────────────────── */
typedef struct {
    __IO uint16_t CTLR1;   uint16_t R0;   /* 0x00 */
    __IO uint16_t CTLR2;   uint16_t R1;   /* 0x04 */
    __IO uint16_t OADDR1;  uint16_t R2;   /* 0x08 */
    __IO uint16_t OADDR2;  uint16_t R3;   /* 0x0C */
    __IO uint16_t DATAR;   uint16_t R4;   /* 0x10 */
    __IO uint16_t STAR1;   uint16_t R5;   /* 0x14 */
    __IO uint16_t STAR2;   uint16_t R6;   /* 0x18 */
    __IO uint16_t CKCFGR;  uint16_t R7;   /* 0x1C */
    __IO uint16_t RTR;     uint16_t R8;   /* 0x20 */
} I2C_TypeDef;

#define I2C1  ((I2C_TypeDef *)(APB1PERIPH_BASE + 0x5400U))

/* CTLR1 */
#define I2C_CTLR1_PE      (1U << 0)
#define I2C_CTLR1_START   (1U << 8)
#define I2C_CTLR1_STOP    (1U << 9)
#define I2C_CTLR1_SWRST   (1U << 15)
/* STAR1 */
#define I2C_STAR1_SB      (1U << 0)
#define I2C_STAR1_ADDR    (1U << 1)
#define I2C_STAR1_BTF     (1U << 2)
#define I2C_STAR1_TXE     (1U << 7)
#define I2C_STAR1_BERR    (1U << 8)
#define I2C_STAR1_ARLO    (1U << 9)
#define I2C_STAR1_AF      (1U << 10)
/* STAR2 */
#define I2C_STAR2_BUSY    (1U << 1)
/* CKCFGR */
#define I2C_CKCFGR_FS     (1U << 15)

/* ── IWDG ─────────────────────────────────────────────────────── */
typedef struct {
    __IO uint32_t CTLR;   /* 0x00 Key register */
    __IO uint32_t PSCR;   /* 0x04 Prescaler */
    __IO uint32_t RLDR;   /* 0x08 Reload */
    __IO uint32_t STATR;  /* 0x0C Status */
} IWDG_TypeDef;

#define IWDG_BASE  (APB1PERIPH_BASE + 0x3000U)
#define IWDG       ((IWDG_TypeDef *)IWDG_BASE)

/* ── ESIG (Electronic Signature) ──────────────────────────────── */
#define ESIG_UID_BASE     0x1FFFF7E8U  /* 96-bit unique ID (12 bytes) */
#define ESIG_FLACAP       0x1FFFF7E0U  /* Flash capacity (16 bits, KB) */

/* ── Clock ────────────────────────────────────────────────────── */
#define HCLK_HZ           96000000U
#define PCLK1_HZ          (HCLK_HZ / 4)
#define STK_TICKS_PER_MS  (HCLK_HZ / 8 / 1000)

/* ── Interrupt enable/disable ─────────────────────────────────── */
static inline void __disable_irq(void) {
    __asm volatile("csrc mstatus, %0" :: "r"(0x88));
}
static inline void __enable_irq(void) {
    __asm volatile("csrs mstatus, %0" :: "r"(0x88));
}

#ifdef __cplusplus
}
#endif

#endif /* CH32V_REGS_H */
