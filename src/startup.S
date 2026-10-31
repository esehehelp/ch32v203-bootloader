/*
 * CH32V203 (CH32V20x_D6) startup — full vector table with weak ISR symbols.
 * SPDX-License-Identifier: MIT
 */

    .section .init,"ax",@progbits
    .global _start
    .align  1
_start:
    j   handle_reset

    /* ── Vector table (reset + 60 entries) ───────────────────── */
    .section .vector,"ax",@progbits
    .align  1
    .global _vector_base
_vector_base:
    .option norvc
    .word   _start                      /*  0  Reset              */
    .word   0x41505031                  /*  1  "1PPA": image marker the bootloader checks */
    .word   NMI_Handler                 /*  2  NMI                */
    .word   HardFault_Handler           /*  3  HardFault          */
    .word   0                           /*  4                     */
    .word   0                           /*  5  Ecall-M            */
    .word   0                           /*  6                     */
    .word   0                           /*  7                     */
    .word   0                           /*  8  Ecall-U            */
    .word   0                           /*  9  Break              */
    .word   0                           /* 10                     */
    .word   0                           /* 11                     */
    .word   SysTick_Handler             /* 12  SysTick            */
    .word   0                           /* 13                     */
    .word   SW_Handler                  /* 14  SW                 */
    .word   0                           /* 15                     */
    /* External interrupts 16..60 */
    .word   WWDG_IRQHandler             /* 16 */
    .word   PVD_IRQHandler              /* 17 */
    .word   TAMPER_IRQHandler           /* 18 */
    .word   RTC_IRQHandler              /* 19 */
    .word   FLASH_IRQHandler            /* 20 */
    .word   RCC_IRQHandler              /* 21 */
    .word   EXTI0_IRQHandler            /* 22 */
    .word   EXTI1_IRQHandler            /* 23 */
    .word   EXTI2_IRQHandler            /* 24 */
    .word   EXTI3_IRQHandler            /* 25 */
    .word   EXTI4_IRQHandler            /* 26 */
    .word   DMA1_Channel1_IRQHandler    /* 27 */
    .word   DMA1_Channel2_IRQHandler    /* 28 */
    .word   DMA1_Channel3_IRQHandler    /* 29 */
    .word   DMA1_Channel4_IRQHandler    /* 30 */
    .word   DMA1_Channel5_IRQHandler    /* 31 */
    .word   DMA1_Channel6_IRQHandler    /* 32 */
    .word   DMA1_Channel7_IRQHandler    /* 33 */
    .word   ADC1_2_IRQHandler           /* 34 */
    .word   USB_HP_CAN1_TX_IRQHandler   /* 35 */
    .word   USB_LP_CAN1_RX0_IRQHandler  /* 36 */
    .word   CAN1_RX1_IRQHandler         /* 37 */
    .word   CAN1_SCE_IRQHandler         /* 38 */
    .word   EXTI9_5_IRQHandler          /* 39 */
    .word   TIM1_BRK_IRQHandler         /* 40 */
    .word   TIM1_UP_IRQHandler          /* 41 */
    .word   TIM1_TRG_COM_IRQHandler     /* 42 */
    .word   TIM1_CC_IRQHandler          /* 43 */
    .word   TIM2_IRQHandler             /* 44 */
    .word   TIM3_IRQHandler             /* 45 */
    .word   TIM4_IRQHandler             /* 46 */
    .word   I2C1_EV_IRQHandler          /* 47 */
    .word   I2C1_ER_IRQHandler          /* 48 */
    .word   I2C2_EV_IRQHandler          /* 49 */
    .word   I2C2_ER_IRQHandler          /* 50 */
    .word   SPI1_IRQHandler             /* 51 */
    .word   SPI2_IRQHandler             /* 52 */
    .word   USART1_IRQHandler           /* 53 */
    .word   USART2_IRQHandler           /* 54 */
    .word   USART3_IRQHandler           /* 55 */
    .word   EXTI15_10_IRQHandler        /* 56 */
    .word   RTCAlarm_IRQHandler         /* 57 */
    .word   USBWakeUp_IRQHandler        /* 58 */
    .word   USBFS_IRQHandler            /* 59 USBFS */
    .word   USBFSWakeUp_IRQHandler      /* 60 */
    .option rvc

    /* ── Weak default handlers ───────────────────────────────── */
    .weak NMI_Handler
    .weak HardFault_Handler
    .weak SysTick_Handler
    .weak SW_Handler
    .weak WWDG_IRQHandler
    .weak PVD_IRQHandler
    .weak TAMPER_IRQHandler
    .weak RTC_IRQHandler
    .weak FLASH_IRQHandler
    .weak RCC_IRQHandler
    .weak EXTI0_IRQHandler
    .weak EXTI1_IRQHandler
    .weak EXTI2_IRQHandler
    .weak EXTI3_IRQHandler
    .weak EXTI4_IRQHandler
    .weak DMA1_Channel1_IRQHandler
    .weak DMA1_Channel2_IRQHandler
    .weak DMA1_Channel3_IRQHandler
    .weak DMA1_Channel4_IRQHandler
    .weak DMA1_Channel5_IRQHandler
    .weak DMA1_Channel6_IRQHandler
    .weak DMA1_Channel7_IRQHandler
    .weak ADC1_2_IRQHandler
    .weak USB_HP_CAN1_TX_IRQHandler
    .weak USB_LP_CAN1_RX0_IRQHandler
    .weak CAN1_RX1_IRQHandler
    .weak CAN1_SCE_IRQHandler
    .weak EXTI9_5_IRQHandler
    .weak TIM1_BRK_IRQHandler
    .weak TIM1_UP_IRQHandler
    .weak TIM1_TRG_COM_IRQHandler
    .weak TIM1_CC_IRQHandler
    .weak TIM2_IRQHandler
    .weak TIM3_IRQHandler
    .weak TIM4_IRQHandler
    .weak I2C1_EV_IRQHandler
    .weak I2C1_ER_IRQHandler
    .weak I2C2_EV_IRQHandler
    .weak I2C2_ER_IRQHandler
    .weak SPI1_IRQHandler
    .weak SPI2_IRQHandler
    .weak USART1_IRQHandler
    .weak USART2_IRQHandler
    .weak USART3_IRQHandler
    .weak EXTI15_10_IRQHandler
    .weak RTCAlarm_IRQHandler
    .weak USBWakeUp_IRQHandler
    .weak USBFS_IRQHandler
    .weak USBFSWakeUp_IRQHandler

    .section .text.default_handler,"ax",@progbits
NMI_Handler:
HardFault_Handler:
SysTick_Handler:
SW_Handler:
WWDG_IRQHandler:
PVD_IRQHandler:
TAMPER_IRQHandler:
RTC_IRQHandler:
FLASH_IRQHandler:
RCC_IRQHandler:
EXTI0_IRQHandler:
EXTI1_IRQHandler:
EXTI2_IRQHandler:
EXTI3_IRQHandler:
EXTI4_IRQHandler:
DMA1_Channel1_IRQHandler:
DMA1_Channel2_IRQHandler:
DMA1_Channel3_IRQHandler:
DMA1_Channel4_IRQHandler:
DMA1_Channel5_IRQHandler:
DMA1_Channel6_IRQHandler:
DMA1_Channel7_IRQHandler:
ADC1_2_IRQHandler:
USB_HP_CAN1_TX_IRQHandler:
USB_LP_CAN1_RX0_IRQHandler:
CAN1_RX1_IRQHandler:
CAN1_SCE_IRQHandler:
EXTI9_5_IRQHandler:
TIM1_BRK_IRQHandler:
TIM1_UP_IRQHandler:
TIM1_TRG_COM_IRQHandler:
TIM1_CC_IRQHandler:
TIM2_IRQHandler:
TIM3_IRQHandler:
TIM4_IRQHandler:
I2C1_EV_IRQHandler:
I2C1_ER_IRQHandler:
I2C2_EV_IRQHandler:
I2C2_ER_IRQHandler:
SPI1_IRQHandler:
SPI2_IRQHandler:
USART1_IRQHandler:
USART2_IRQHandler:
USART3_IRQHandler:
EXTI15_10_IRQHandler:
RTCAlarm_IRQHandler:
USBWakeUp_IRQHandler:
USBFS_IRQHandler:
USBFSWakeUp_IRQHandler:
1:  j   1b

    /* ── Reset handler ───────────────────────────────────────── */
    .section .text.handle_reset,"ax",@progbits
    .weak   handle_reset
    .align  1
handle_reset:
.option push
.option norelax
    la  gp, __global_pointer$
.option pop
    la  sp, _eusrstack

    /* Copy .data from flash to RAM */
    la  a0, _data_lma
    la  a1, _data_vma
    la  a2, _edata
    bgeu a1, a2, 2f
1:  lw  t0, (a0)
    sw  t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b
2:
    /* Clear .bss */
    la  a0, _sbss
    la  a1, _ebss
    bgeu a0, a1, 2f
1:  sw  zero, (a0)
    addi a0, a0, 4
    bltu a0, a1, 1b
2:
    /* Pipeline + instruction prediction */
    li  t0, 0x1f
    csrw 0xbc0, t0
    /* INTSYSCR: hardware stack off, nesting off — ISRs use the
     * standard interrupt attribute and save their own context. */
    csrw 0x804, zero
    /* Vector table: absolute vectored mode (mode 3) */
    la  t0, _vector_base
    ori t0, t0, 3
    csrw mtvec, t0
    /* Global interrupt enable after mret (MPIE, M-mode) */
    li  t0, 0x1880
    csrw mstatus, t0

    /* Jump to main */
    la  t0, main
    csrw mepc, t0
    mret
