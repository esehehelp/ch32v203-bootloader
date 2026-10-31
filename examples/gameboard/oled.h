/*
 * oled.h — 128x64 I2C OLED (SSD1306 / SH1106) on I2C1 (PB6/PB7).
 * SPDX-License-Identifier: MIT
 */
#ifndef OLED_DRV_H
#define OLED_DRV_H

#include <stdint.h>

#define OLED_W  128
#define OLED_H  64

typedef enum { OLED_SSD1306 = 0, OLED_SH1106 = 1 } oled_ctl_t;

/* I2C */
void i2c_init(uint32_t hz);
int  i2c_probe(uint8_t addr7);                    /* 0 = ACK */
int  i2c_write(uint8_t addr7, uint8_t ctrl, const uint8_t *buf, uint16_t len);

/* Display */
int  oled_init(uint8_t addr7, oled_ctl_t ctl);    /* 0 = OK */
int  oled_flush(void);                            /* 0 = OK */
void oled_contrast(uint8_t c);
void oled_invert(int on);
void oled_flip(int on);                           /* rotate 180° (default on) */
int  oled_flipped(void);
uint8_t    oled_addr(void);
oled_ctl_t oled_ctl(void);

/* Drawing into the framebuffer (call oled_flush to show) */
extern uint8_t oled_fb[OLED_W * OLED_H / 8];
void oled_clear(void);
void oled_fill(uint8_t pattern);
void oled_pixel(int x, int y, int on);
void oled_hline(int x, int y, int w, int on);
void oled_vline(int x, int y, int h, int on);
void oled_line(int x0, int y0, int x1, int y1, int on);
void oled_rect(int x, int y, int w, int h, int on);
void oled_fill_rect(int x, int y, int w, int h, int on);
void oled_circle(int cx, int cy, int r, int on);
void oled_fill_circle(int cx, int cy, int r, int on);
int  oled_char(int x, int y, char c, int scale);  /* returns advance */
int  oled_text(int x, int y, const char *s, int scale);

#endif /* OLED_DRV_H */
