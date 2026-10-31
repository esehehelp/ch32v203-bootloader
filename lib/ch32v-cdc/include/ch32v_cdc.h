/*
 * ch32v_cdc.h — USB CDC-ACM library for CH32V203 (USBFS on PA11/PA12).
 * Port of ch32x-cdc (CH32X035) by the same API.
 * SPDX-License-Identifier: MIT
 */
#ifndef CH32V_CDC_H
#define CH32V_CDC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      magic_baud_enable;  /* 1200bps reboot trigger (default: on) */
    uint32_t magic_baud;         /* default: 1200 */
    void (*pre_reboot)(void);    /* called before bootloader entry */
} ch32v_cdc_config_t;

/* USB IDs: the app and the bootloader enumerate differently so the upload
 * tool can tell them apart. Build the bootloader with -DCH32V_CDC_BOOTLOADER. */
#define CH32V_CDC_VID       0x1A86
#define CH32V_CDC_PID_APP   0xFE0D
#define CH32V_CDC_PID_BOOT  0xFE0E

/* Written to _boot_magic (last RAM word, kept across reset) to make the
 * bootloader stay resident instead of starting the app. */
#define CH32V_BOOT_MAGIC    0xB007B007U

/* Init / reboot */
void     ch32v_clock_init(void);              /* HSI→PLL 96 MHz, USB 48 MHz */
void     ch32v_cdc_init(const ch32v_cdc_config_t *cfg);
void     ch32v_cdc_reboot_to_bootloader(void);

/* State */
int      ch32v_cdc_connected(void);           /* host configured the device */

/* Read */
int      ch32v_cdc_available(void);
int      ch32v_cdc_peek(void);
int      ch32v_cdc_read(void);
size_t   ch32v_cdc_read_buf(uint8_t *buf, size_t max);

/* Write */
size_t   ch32v_cdc_write(const uint8_t *buf, size_t len);
size_t   ch32v_cdc_print(const char *s);
void     ch32v_cdc_flush(void);

/* Poll — call from main loop; handles deferred reboot */
void     ch32v_cdc_poll(void);

/* Weak callback: override in app to handle RX in ISR context */
void     ch32v_cdc_on_rx(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CH32V_CDC_H */
