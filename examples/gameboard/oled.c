/*
 * oled.c — 128x64 I2C OLED (SSD1306 / SH1106) on hardware I2C1 (PB6/PB7).
 * SPDX-License-Identifier: MIT
 *
 * Every bus wait has a timeout so a missing or wedged display never stalls
 * the main loop (which also services the 1200bps BootROM trigger).
 */

#include "oled.h"
#include "board.h"
#include "font5x7.h"
#include <string.h>

#define I2C_TIMEOUT_MS  5

uint8_t oled_fb[OLED_W * OLED_H / 8];

static uint8_t    cur_addr = 0x3C;
static oled_ctl_t cur_ctl  = OLED_SSD1306;
static uint32_t   cur_hz   = 400000;
static int        cur_flip = 1;

/* ══════════════════════════════════════════════════════════════
 *  I2C1
 * ══════════════════════════════════════════════════════════════ */

/* Clock out a stuck slave: 9 SCL pulses then a STOP, by bit-banging */
static void i2c_bus_recover(void) {
    GPIOB->BSHR = (1U << 6) | (1U << 7);
    gpio_cfg(GPIOB, 6, GPIO_CFG_OUT_OD_50M);
    gpio_cfg(GPIOB, 7, GPIO_CFG_OUT_OD_50M);
    for (int i = 0; i < 9; i++) {
        GPIOB->BCR  = 1U << 6;
        for (volatile int d = 0; d < 200; d++) {}
        GPIOB->BSHR = 1U << 6;
        for (volatile int d = 0; d < 200; d++) {}
    }
    GPIOB->BCR  = 1U << 7;               /* SDA low while SCL high … */
    for (volatile int d = 0; d < 200; d++) {}
    GPIOB->BSHR = 1U << 7;               /* … then release = STOP */
    for (volatile int d = 0; d < 200; d++) {}
}

void i2c_init(uint32_t hz) {
    cur_hz = hz;
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO;
    RCC->APB1PCENR |= RCC_APB1Periph_I2C1;

    i2c_bus_recover();
    gpio_cfg(GPIOB, 6, GPIO_CFG_AF_OD_50M);
    gpio_cfg(GPIOB, 7, GPIO_CFG_AF_OD_50M);

    RCC->APB1PRSTR |= RCC_APB1Periph_I2C1;
    RCC->APB1PRSTR &= ~RCC_APB1Periph_I2C1;

    const uint32_t mhz = PCLK1_HZ / 1000000;
    I2C1->CTLR1 = 0;
    I2C1->CTLR2 = (uint16_t)mhz;
    if (hz > 100000) {
        uint32_t ccr = PCLK1_HZ / (3 * hz);           /* Tlow:Thigh = 2:1 */
        if (ccr < 1) ccr = 1;
        I2C1->CKCFGR = (uint16_t)(I2C_CKCFGR_FS | ccr);
        I2C1->RTR    = (uint16_t)(mhz * 300 / 1000 + 1);
    } else {
        uint32_t ccr = PCLK1_HZ / (2 * hz);
        if (ccr < 4) ccr = 4;
        I2C1->CKCFGR = (uint16_t)ccr;
        I2C1->RTR    = (uint16_t)(mhz + 1);
    }
    I2C1->CTLR1 = I2C_CTLR1_PE;
}

/* Wait until (STAR1 & mask) is set. Returns 0 ok, -1 NACK, -2 timeout. */
static int wait_star1(uint16_t mask) {
    uint32_t t0 = millis();
    for (;;) {
        uint16_t s = I2C1->STAR1;
        if (s & mask) return 0;
        if (s & (I2C_STAR1_AF | I2C_STAR1_BERR | I2C_STAR1_ARLO)) {
            I2C1->STAR1 = (uint16_t)~(I2C_STAR1_AF | I2C_STAR1_BERR | I2C_STAR1_ARLO);
            return -1;
        }
        if (millis() - t0 > I2C_TIMEOUT_MS) return -2;
    }
}

static int i2c_begin(uint8_t addr7) {
    uint32_t t0 = millis();
    while (I2C1->STAR2 & I2C_STAR2_BUSY) {
        if (millis() - t0 > I2C_TIMEOUT_MS) { i2c_init(cur_hz); break; }
    }
    I2C1->CTLR1 |= I2C_CTLR1_START;
    int r = wait_star1(I2C_STAR1_SB);
    if (r) return r;
    I2C1->DATAR = (uint16_t)(addr7 << 1);
    r = wait_star1(I2C_STAR1_ADDR);
    if (r) return r;
    (void)I2C1->STAR1;
    (void)I2C1->STAR2;                   /* clears ADDR */
    return 0;
}

static int i2c_end(int r) {
    if (r == 0) r = wait_star1(I2C_STAR1_BTF);
    I2C1->CTLR1 |= I2C_CTLR1_STOP;
    if (r == -2) i2c_init(cur_hz);       /* wedged: reset peripheral + bus */
    return r;
}

int i2c_probe(uint8_t addr7) {
    int r = i2c_begin(addr7);
    I2C1->CTLR1 |= I2C_CTLR1_STOP;
    if (r == -2) i2c_init(cur_hz);
    return r;
}

int i2c_write(uint8_t addr7, uint8_t ctrl, const uint8_t *buf, uint16_t len) {
    int r = i2c_begin(addr7);
    if (r) return i2c_end(r);
    I2C1->DATAR = ctrl;
    for (uint16_t i = 0; i < len; i++) {
        r = wait_star1(I2C_STAR1_TXE);
        if (r) return i2c_end(r);
        I2C1->DATAR = buf[i];
    }
    return i2c_end(0);
}

/* ══════════════════════════════════════════════════════════════
 *  Controller
 * ══════════════════════════════════════════════════════════════ */

static int cmds(const uint8_t *c, uint16_t n) {
    return i2c_write(cur_addr, 0x00, c, n);
}

static const uint8_t init_ssd1306[] = {
    0xAE,             /* display off */
    0xD5, 0x80,       /* clock divide */
    0xA8, 0x3F,       /* multiplex 64 */
    0xD3, 0x00,       /* display offset */
    0x40,             /* start line 0 */
    0x8D, 0x14,       /* charge pump on */
    0x20, 0x02,       /* page addressing mode */
    0xA1,             /* segment remap */
    0xC8,             /* COM scan descending */
    0xDA, 0x12,       /* COM pins */
    0x81, 0xCF,       /* contrast */
    0xD9, 0xF1,       /* pre-charge */
    0xDB, 0x40,       /* VCOMH */
    0xA4,             /* resume to RAM */
    0xA6,             /* normal (not inverted) */
    0xAF,             /* display on */
};

static const uint8_t init_sh1106[] = {
    0xAE,
    0xD5, 0x80,
    0xA8, 0x3F,
    0xD3, 0x00,
    0x40,
    0xAD, 0x8B,       /* DC-DC on */
    0xA1,
    0xC8,
    0xDA, 0x12,
    0x81, 0xCF,
    0xD9, 0x22,
    0xDB, 0x35,
    0xA4,
    0xA6,
    0xAF,
};

int oled_init(uint8_t addr7, oled_ctl_t ctl) {
    cur_addr = addr7;
    cur_ctl  = ctl;
    int r = (ctl == OLED_SH1106) ? cmds(init_sh1106, sizeof(init_sh1106))
                                 : cmds(init_ssd1306, sizeof(init_ssd1306));
    if (r == 0) oled_flip(cur_flip);
    return r;
}

/* The panel is mounted upside down relative to the init tables'
 * A1/C8 orientation, so the default is the A0/C0 (180°) variant. */
void oled_flip(int on) {
    cur_flip = on;
    uint8_t c[2] = { on ? 0xA0 : 0xA1, on ? 0xC0 : 0xC8 };
    cmds(c, 2);
}

int oled_flipped(void) { return cur_flip; }

int oled_flush(void) {
    /* Page-mode writes work on both controllers; SH1106 has a 132-column
     * RAM with the visible window starting at column 2. */
    const uint8_t col = (cur_ctl == OLED_SH1106) ? 2 : 0;
    for (uint8_t p = 0; p < OLED_H / 8; p++) {
        uint8_t c[3] = { (uint8_t)(0xB0 | p), (uint8_t)(col & 0x0F), (uint8_t)(0x10 | (col >> 4)) };
        int r = cmds(c, 3);
        if (r) return r;
        r = i2c_write(cur_addr, 0x40, &oled_fb[p * OLED_W], OLED_W);
        if (r) return r;
    }
    return 0;
}

void oled_contrast(uint8_t v) {
    uint8_t c[2] = { 0x81, v };
    cmds(c, 2);
}

void oled_invert(int on) {
    uint8_t c = on ? 0xA7 : 0xA6;
    cmds(&c, 1);
}

uint8_t    oled_addr(void) { return cur_addr; }
oled_ctl_t oled_ctl(void)  { return cur_ctl; }

/* ══════════════════════════════════════════════════════════════
 *  Drawing
 * ══════════════════════════════════════════════════════════════ */

void oled_clear(void) { memset(oled_fb, 0, sizeof(oled_fb)); }
void oled_fill(uint8_t pattern) { memset(oled_fb, pattern, sizeof(oled_fb)); }

void oled_pixel(int x, int y, int on) {
    if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
    uint8_t *b = &oled_fb[(y >> 3) * OLED_W + x];
    uint8_t m = (uint8_t)(1U << (y & 7));
    if (on > 0)       *b |= m;
    else if (on == 0) *b &= (uint8_t)~m;
    else              *b ^= m;           /* on < 0: XOR */
}

void oled_hline(int x, int y, int w, int on) { for (int i = 0; i < w; i++) oled_pixel(x + i, y, on); }
void oled_vline(int x, int y, int h, int on) { for (int i = 0; i < h; i++) oled_pixel(x, y + i, on); }

void oled_line(int x0, int y0, int x1, int y1, int on) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        oled_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void oled_rect(int x, int y, int w, int h, int on) {
    oled_hline(x, y, w, on);
    oled_hline(x, y + h - 1, w, on);
    oled_vline(x, y + 1, h - 2, on);
    oled_vline(x + w - 1, y + 1, h - 2, on);
}

void oled_fill_rect(int x, int y, int w, int h, int on) {
    for (int j = 0; j < h; j++) oled_hline(x, y + j, w, on);
}

void oled_circle(int cx, int cy, int r, int on) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        oled_pixel(cx + x, cy + y, on); oled_pixel(cx - x, cy + y, on);
        oled_pixel(cx + x, cy - y, on); oled_pixel(cx - x, cy - y, on);
        oled_pixel(cx + y, cy + x, on); oled_pixel(cx - y, cy + x, on);
        oled_pixel(cx + y, cy - x, on); oled_pixel(cx - y, cy - x, on);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void oled_fill_circle(int cx, int cy, int r, int on) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) oled_pixel(cx + dx, cy + dy, on);
}

int oled_char(int x, int y, char c, int scale) {
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    const uint8_t *g = font5x7[c - FONT_FIRST];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            if (g[col] & (1U << row))
                oled_fill_rect(x + col * scale, y + row * scale, scale, scale, 1);
    return 6 * scale;
}

int oled_text(int x, int y, const char *s, int scale) {
    int x0 = x;
    while (*s) x += oled_char(x, y, *s++, scale);
    return x - x0;
}
