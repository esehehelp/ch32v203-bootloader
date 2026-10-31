/*
 * Sample app: USB CDC serial echo.
 * SPDX-License-Identifier: MIT
 *
 * Runs at 0x2000 behind the CDC bootloader; `pio run -t upload` reflashes it
 * through the 1200bps touch. Replace this file with your own application.
 */

#include "ch32v_regs.h"
#include "ch32v_cdc.h"

void NMI_Handler(void)       __attribute__((interrupt));
void HardFault_Handler(void) __attribute__((interrupt));
void NMI_Handler(void)       {}
void HardFault_Handler(void) { ch32v_cdc_reboot_to_bootloader(); }

int main(void) {
    ch32v_cdc_init(NULL);

    for (;;) {
        uint8_t buf[64];
        size_t n = ch32v_cdc_read_buf(buf, sizeof(buf));
        if (n > 0)
            ch32v_cdc_write(buf, n);
        ch32v_cdc_poll();
        IWDG->CTLR = 0xAAAA;
    }
}
