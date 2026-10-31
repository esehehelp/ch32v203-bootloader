/*
 * CH32V203K8T6 gameboard — display bring-up app.
 * SPDX-License-Identifier: MIT
 *
 * USB CDC serial shell (type "help") + OLED demo screens.
 * Long-press ENTER (PA1) cycles screens. 1200bps touch reboots into the
 * USB bootloader (src/boot).
 */

#include "ch32v_regs.h"
#include "ch32v_cdc.h"
#include "board.h"
#include "oled.h"
#include <string.h>

void NMI_Handler(void)       __attribute__((interrupt));
void HardFault_Handler(void) __attribute__((interrupt));
void NMI_Handler(void)       {}
void HardFault_Handler(void) { ch32v_cdc_reboot_to_bootloader(); }

/* ══════════════════════════════════════════════════════════════
 *  Small print helpers (no printf, keeps the image small)
 * ══════════════════════════════════════════════════════════════ */
static void out(const char *s) { ch32v_cdc_print(s); }

static char *fmt_u(char *p, uint32_t v) {
    char t[11]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static char *fmt_x(char *p, uint32_t v, int digits) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) *p++ = hex[(v >> (i * 4)) & 0xF];
    *p = 0;
    return p;
}

static void out_u(uint32_t v) { char b[12]; fmt_u(b, v); out(b); }
static void out_x(uint32_t v, int d) { char b[12]; fmt_x(b, v, d); out(b); }

/* ══════════════════════════════════════════════════════════════
 *  Buzzer: TIM1_CH2 PWM on PA9
 * ══════════════════════════════════════════════════════════════ */
typedef struct {
    __IO uint16_t CTLR1;     uint16_t R0;    /* 0x00 */
    __IO uint16_t CTLR2;     uint16_t R1;    /* 0x04 */
    __IO uint16_t SMCFGR;    uint16_t R2;    /* 0x08 */
    __IO uint16_t DMAINTENR; uint16_t R3;    /* 0x0C */
    __IO uint16_t INTFR;     uint16_t R4;    /* 0x10 */
    __IO uint16_t SWEVGR;    uint16_t R5;    /* 0x14 */
    __IO uint16_t CHCTLR1;   uint16_t R6;    /* 0x18 */
    __IO uint16_t CHCTLR2;   uint16_t R7;    /* 0x1C */
    __IO uint16_t CCER;      uint16_t R8;    /* 0x20 */
    __IO uint16_t CNT;       uint16_t R9;    /* 0x24 */
    __IO uint16_t PSC;       uint16_t R10;   /* 0x28 */
    __IO uint16_t ATRLR;     uint16_t R11;   /* 0x2C */
    __IO uint16_t RPTCR;     uint16_t R12;   /* 0x30 */
    __IO uint16_t CH1CVR;    uint16_t R13;   /* 0x34 */
    __IO uint16_t CH2CVR;    uint16_t R14;   /* 0x38 */
    __IO uint16_t CH3CVR;    uint16_t R15;   /* 0x3C */
    __IO uint16_t CH4CVR;    uint16_t R16;   /* 0x40 */
    __IO uint16_t BDTR;      uint16_t R17;   /* 0x44 */
} TIM_TypeDef;
#define TIM1                 ((TIM_TypeDef *)(APB2PERIPH_BASE + 0x2C00U))
#define RCC_APB2Periph_TIM1  (1U << 11)

static uint32_t beep_until;
static uint8_t  beeping;
static uint8_t  sound_on = 1;

static void buzzer_off(void) {
    TIM1->CCER = 0;
    TIM1->CTLR1 = 0;
    GPIOA->BCR = 1U << BUZZ_PIN;
    gpio_cfg(GPIOA, BUZZ_PIN, GPIO_CFG_OUT_PP_50M);
    beeping = 0;
}

static void beep(uint32_t hz, uint32_t ms) {
    if (!sound_on || hz < 50 || hz > 20000) return;
    RCC->APB2PCENR |= RCC_APB2Periph_TIM1;
    /* TIM1 clock = PCLK2 = HCLK = 96 MHz → 1 MHz count */
    TIM1->PSC     = (uint16_t)(HCLK_HZ / 1000000 - 1);
    TIM1->ATRLR   = (uint16_t)(1000000 / hz - 1);
    TIM1->CH2CVR  = (uint16_t)(500000 / hz);
    TIM1->CHCTLR1 = 0x6800;           /* OC2M = PWM1, OC2PE */
    TIM1->CCER    = 0x0010;           /* CC2E */
    TIM1->BDTR    = 0x8000;           /* MOE: advanced timer main output */
    TIM1->SWEVGR  = 0x0001;           /* UG: load PSC/ARR */
    TIM1->CTLR1   = 0x0081;           /* ARPE | CEN */
    gpio_cfg(GPIOA, BUZZ_PIN, 0xB);   /* AF push-pull 50 MHz */
    beep_until = millis() + ms;
    beeping = 1;
}

/* ══════════════════════════════════════════════════════════════
 *  Buttons
 * ══════════════════════════════════════════════════════════════ */
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_ENTER, B_COUNT };

typedef struct {
    const char *name;
    uint8_t     pin;
    uint16_t    tone;
    uint8_t     down;       /* debounced state */
    uint8_t     pressed;    /* edge: became down this loop */
    uint32_t    t_change;
    uint32_t    count;
} button_t;

static button_t btn[B_COUNT] = {
    [B_UP]    = { "UP",    BTN_UP,    1047 },
    [B_DOWN]  = { "DOWN",  BTN_DOWN,  784  },
    [B_LEFT]  = { "LEFT",  BTN_LEFT,  880  },
    [B_RIGHT] = { "RIGHT", BTN_RIGHT, 988  },
    [B_ENTER] = { "ENTER", BTN_ENTER, 1319 },
};

static void buttons_init(void) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB;
    /* Pull-ups on every monitored pin (buttons included) */
    for (unsigned p = 0; p < 16; p++) {
        if (MON_PA_MASK & (1U << p)) { gpio_cfg(GPIOA, p, GPIO_CFG_IN_PUPD); GPIOA->BSHR = 1U << p; }
        if (MON_PB_MASK & (1U << p)) { gpio_cfg(GPIOB, p, GPIO_CFG_IN_PUPD); GPIOB->BSHR = 1U << p; }
    }
}

static void buttons_scan(uint32_t now) {
    uint32_t in = GPIOA->INDR;
    for (int i = 0; i < B_COUNT; i++) {
        button_t *b = &btn[i];
        uint8_t raw = !(in & (1U << b->pin));
        b->pressed = 0;
        if (raw != b->down && now - b->t_change > 20) {
            b->down = raw;
            b->t_change = now;
            if (raw) { b->pressed = 1; b->count++; }
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 *  Display state
 * ══════════════════════════════════════════════════════════════ */
enum { MODE_BUTTONS, MODE_INFO, MODE_BALLS, MODE_PATTERN, MODE_TERM, MODE_BIG, MODE_COUNT };

static int      oled_ok;
static int      mode = MODE_BUTTONS;
static uint32_t frames, fps, fps_t0, fps_frames;
static uint32_t i2c_errors;

#define TERM_ROWS 8
#define TERM_COLS 21
static char term[TERM_ROWS][TERM_COLS + 1];
static int  term_row;

static void term_puts(const char *s) {
    term_row = (term_row + 1) % TERM_ROWS;
    strncpy(term[term_row], s, TERM_COLS);
    term[term_row][TERM_COLS] = 0;
}

static const uint8_t scan_addrs[] = { 0x3C, 0x3D };

static int display_detect(void) {
    for (unsigned i = 0; i < sizeof(scan_addrs); i++) {
        if (i2c_probe(scan_addrs[i]) == 0)
            return oled_init(scan_addrs[i], oled_ctl()) == 0;
    }
    return 0;
}

/* ── Screens ─────────────────────────────────────────────────── */
static void draw_title(const char *s) {
    oled_text((OLED_W - (int)strlen(s) * 6) / 2, 1, s, 1);
    oled_fill_rect(0, 0, OLED_W, 9, -1);     /* inverted title bar */
}

static void draw_buttons(void) {
    char b[24], *p;
    draw_title("BUTTON TEST");

    /* D-pad */
    const int cx = 24, cy = 33, s = 11;
    struct { int i, x, y; } pad[4] = {
        { B_UP,    cx - s / 2,         cy - s / 2 - s - 1 },
        { B_DOWN,  cx - s / 2,         cy + s / 2 + 2     },
        { B_LEFT,  cx - s / 2 - s - 1, cy - s / 2         },
        { B_RIGHT, cx + s / 2 + 2,     cy - s / 2         },
    };
    for (int k = 0; k < 4; k++) {
        if (btn[pad[k].i].down) oled_fill_rect(pad[k].x, pad[k].y, s, s, 1);
        else                    oled_rect(pad[k].x, pad[k].y, s, s, 1);
    }
    /* ENTER */
    if (btn[B_ENTER].down) oled_fill_circle(112, 33, 9, 1);
    else                   oled_circle(112, 33, 9, 1);
    oled_text(100, 45, "ENT", 1);

    /* Press counters */
    static const char tag[B_COUNT] = { 'U', 'D', 'L', 'R', 'E' };
    for (int i = 0; i < B_COUNT; i++) {
        p = b; *p++ = tag[i]; *p++ = ' '; *p++ = 'A';
        p = fmt_u(p, btn[i].pin);
        if (btn[i].pin < 10) *p++ = ' ';
        *p++ = ' ';
        fmt_u(p, btn[i].count);
        oled_text(48, 11 + i * 8, b, 1);
    }

    /* Raw monitor: every monitored pin that currently reads low */
    uint32_t pa = ~GPIOA->INDR & MON_PA_MASK, pb = ~GPIOB->INDR & MON_PB_MASK;
    p = b; memcpy(p, "low:", 4); p += 4; *p = 0;
    for (unsigned k = 0; k < 16 && p < b + sizeof(b) - 5; k++)
        if (pa & (1U << k)) { *p++ = ' '; *p++ = 'A'; p = fmt_u(p, k); }
    for (unsigned k = 0; k < 16 && p < b + sizeof(b) - 5; k++)
        if (pb & (1U << k)) { *p++ = ' '; *p++ = 'B'; p = fmt_u(p, k); }
    oled_text(0, 56, b, 1);
}

static void draw_info(void) {
    char b[24], *p;
    draw_title("CH32V203 GAMEBOARD");

    oled_text(0, 12, ch32v_cdc_connected() ? "USB : connected" : "USB : --", 1);

    /* USBD register snapshot, for bring-up */
    p = b; memcpy(p, "USBD c", 6); p += 6; p = fmt_x(p, USBD_CNTR, 4);
    memcpy(p, " a", 2); p += 2; p = fmt_x(p, USBD_DADDR, 2);
    memcpy(p, " f", 2); p += 2; p = fmt_x(p, USBD_FNR & 0x7FF, 3);
    oled_text(0, 21, b, 1);

    p = b; memcpy(p, "OLED: 0x", 8); p += 8; p = fmt_x(p, oled_addr(), 2);
    memcpy(p, oled_ctl() == OLED_SH1106 ? " SH1106" : " SSD1306", 9);
    oled_text(0, 30, b, 1);

    uint32_t ms = millis();
    p = b; memcpy(p, "up  : ", 6); p += 6; p = fmt_u(p, ms / 1000);
    *p++ = '.'; p = fmt_u(p, (ms / 100) % 10); *p++ = 's'; *p = 0;
    oled_text(0, 39, b, 1);

    p = b; memcpy(p, "fps : ", 6); p += 6; p = fmt_u(p, fps);
    memcpy(p, "  err ", 6); p += 6; p = fmt_u(p, i2c_errors);
    oled_text(0, 48, b, 1);

    oled_text(0, 57, "hold ENTER: next", 1);
}

static void draw_balls(void) {
    static int init;
    static int x[3], y[3], vx[3], vy[3];
    static const int r[3] = { 6, 4, 3 };
    if (!init) {
        x[0] = 20 << 4; y[0] = 20 << 4; vx[0] =  23; vy[0] =  17;
        x[1] = 60 << 4; y[1] = 30 << 4; vx[1] = -31; vy[1] =  12;
        x[2] = 90 << 4; y[2] = 15 << 4; vx[2] =  19; vy[2] = -27;
        init = 1;
    }
    /* D-pad pushes the big ball */
    if (btn[B_UP].down)    vy[0] -= 2;
    if (btn[B_DOWN].down)  vy[0] += 2;
    if (btn[B_LEFT].down)  vx[0] -= 2;
    if (btn[B_RIGHT].down) vx[0] += 2;

    oled_rect(0, 0, OLED_W, OLED_H, 1);
    for (int i = 0; i < 3; i++) {
        x[i] += vx[i]; y[i] += vy[i];
        if (x[i] < (r[i] + 1) << 4) { x[i] = (r[i] + 1) << 4; vx[i] = -vx[i]; }
        if (x[i] > (OLED_W - 2 - r[i]) << 4) { x[i] = (OLED_W - 2 - r[i]) << 4; vx[i] = -vx[i]; }
        if (y[i] < (r[i] + 1) << 4) { y[i] = (r[i] + 1) << 4; vy[i] = -vy[i]; }
        if (y[i] > (OLED_H - 2 - r[i]) << 4) { y[i] = (OLED_H - 2 - r[i]) << 4; vy[i] = -vy[i]; }
        if (i == 0) oled_fill_circle(x[i] >> 4, y[i] >> 4, r[i], 1);
        else        oled_circle(x[i] >> 4, y[i] >> 4, r[i], 1);
    }
}

static void draw_pattern(void) {
    switch ((millis() / 1500) % 6) {
    case 0: oled_fill(0xFF); break;                                   /* all on */
    case 1: for (int i = 0; i < (int)sizeof(oled_fb); i++)           /* checker */
                oled_fb[i] = (i & 1) ? 0xAA : 0x55;
            break;
    case 2: for (int i = 0; i < (int)sizeof(oled_fb); i++)           /* v-stripes */
                oled_fb[i] = (i & 1) ? 0xFF : 0x00;
            break;
    case 3: oled_fill(0x55); break;                                   /* h-stripes */
    case 4: for (int x = 0; x < OLED_W; x += 8) oled_vline(x, 0, OLED_H, 1);
            for (int y = 0; y < OLED_H; y += 8) oled_hline(0, y, OLED_W, 1);
            oled_rect(0, 0, OLED_W, OLED_H, 1);
            break;
    case 5: oled_rect(0, 0, OLED_W, OLED_H, 1);                       /* corners */
            oled_line(0, 0, OLED_W - 1, OLED_H - 1, 1);
            oled_line(OLED_W - 1, 0, 0, OLED_H - 1, 1);
            oled_text(46, 28, "128x64", 1);
            break;
    }
}

static void draw_term(void) {
    for (int i = 0; i < TERM_ROWS; i++) {
        int row = (term_row + 1 + i) % TERM_ROWS;
        oled_text(0, i * 8, term[row], 1);
    }
}

static void draw_big(void) {
    char b[12];
    uint32_t s = millis() / 1000;
    char *p = fmt_u(b, s / 60);
    *p++ = ':';
    *p++ = (char)('0' + (s % 60) / 10);
    *p++ = (char)('0' + s % 10);
    *p = 0;
    int w = (int)strlen(b) * 18;
    oled_text((OLED_W - w) / 2, 12, b, 3);
    int bar = (int)((millis() % 1000) * (OLED_W - 8) / 1000);
    oled_rect(2, 48, OLED_W - 4, 10, 1);
    oled_fill_rect(4, 50, bar, 6, 1);
}

static void render(void) {
    oled_clear();
    switch (mode) {
    case MODE_BUTTONS: draw_buttons(); break;
    case MODE_INFO:    draw_info();    break;
    case MODE_BALLS:   draw_balls();   break;
    case MODE_PATTERN: draw_pattern(); break;
    case MODE_TERM:    draw_term();    break;
    case MODE_BIG:     draw_big();     break;
    }
    if (oled_flush() != 0) {
        i2c_errors++;
        oled_ok = 0;
    }
    frames++;
}

/* ══════════════════════════════════════════════════════════════
 *  Serial shell
 * ══════════════════════════════════════════════════════════════ */
static char line[64];
static int  line_len;

static int parse_num(const char **s, uint32_t *v) {
    const char *p = *s;
    while (*p == ' ') p++;
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    uint32_t r = 0; int n = 0;
    for (;; p++, n++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (base == 16 && *p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (base == 16 && *p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        r = r * (uint32_t)base + (uint32_t)d;
    }
    *s = p;
    *v = r;
    return n > 0;
}

static int starts(const char *s, const char *cmd, const char **rest) {
    size_t n = strlen(cmd);
    if (strncmp(s, cmd, n) || (s[n] && s[n] != ' ')) return 0;
    *rest = s + n;
    return 1;
}

static void cmd_scan(void) {
    out("I2C scan:");
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_probe(a) == 0) { out(" 0x"); out_x(a, 2); found++; }
    }
    out(found ? "\r\n" : " none\r\n");
}

static void cmd_peek(uint32_t addr, uint32_t len) {
    if (len == 0) len = 64;
    if (len > 512) len = 512;
    addr &= ~3U;
    for (uint32_t i = 0; i < len; i += 16) {
        out_x(addr + i, 8); out(":");
        for (uint32_t j = 0; j < 16 && i + j < len; j += 4) {
            out(" ");
            out_x(*(volatile uint32_t *)(addr + i + j), 8);
        }
        out("\r\n");
        ch32v_cdc_flush();
    }
}

static void cmd_info(void) {
    out("CH32V203 gameboard  flash "); out_u(*(volatile uint16_t *)ESIG_FLACAP);
    out("K  UID ");
    for (int i = 0; i < 12; i++) out_x(((volatile uint8_t *)ESIG_UID_BASE)[i], 2);
    out("\r\nOLED "); out(oled_ok ? "ok" : "not found"); out(" addr 0x"); out_x(oled_addr(), 2);
    out(oled_ctl() == OLED_SH1106 ? " SH1106" : " SSD1306");
    out("  mode "); out_u((uint32_t)mode); out("  fps "); out_u(fps);
    out("  i2c errors "); out_u(i2c_errors); out("\r\nbuttons:");
    for (int i = 0; i < B_COUNT; i++) {
        out(" "); out(btn[i].name); out("(PA"); out_u(btn[i].pin); out(")=");
        out_u(btn[i].count);
    }
    out("\r\n");
}

static void run_line(const char *s) {
    const char *a;
    uint32_t v, w;
    while (*s == ' ') s++;
    if (!*s) return;

    if (starts(s, "help", &a)) {
        out("commands:\r\n"
            "  info                 status + button counters\r\n"
            "  scan                 I2C bus scan\r\n"
            "  mode <0-5>           0 buttons 1 info 2 balls 3 pattern 4 term 5 clock\r\n"
            "  say <text>           print text on the term screen\r\n"
            "  ctl ssd1306|sh1106   select display controller\r\n"
            "  addr <0x3c>          display I2C address\r\n"
            "  speed <hz>           I2C clock (100000 / 400000)\r\n"
            "  contrast <0-255>     display contrast\r\n"
            "  invert <0|1>         invert display\r\n"
            "  flip <0|1>           rotate display 180 degrees\r\n"
            "  beep [hz] [ms]       buzzer on PA9\r\n"
            "  sound <0|1>          key click on/off\r\n"
            "  peek <addr> [len]    hex dump memory\r\n"
            "  boot                 reboot into the USB bootloader\r\n"
            "  reset                system reset\r\n");
    } else if (starts(s, "info", &a)) {
        cmd_info();
    } else if (starts(s, "scan", &a)) {
        cmd_scan();
    } else if (starts(s, "mode", &a) && parse_num(&a, &v)) {
        mode = (int)(v % MODE_COUNT);
        out("ok\r\n");
    } else if (starts(s, "say", &a)) {
        while (*a == ' ') a++;
        term_puts(a);
        mode = MODE_TERM;
        out("ok\r\n");
    } else if (starts(s, "ctl", &a)) {
        while (*a == ' ') a++;
        oled_ctl_t c = (strcmp(a, "sh1106") == 0) ? OLED_SH1106 : OLED_SSD1306;
        oled_ok = oled_init(oled_addr(), c) == 0;
        out(oled_ok ? "ok\r\n" : "init failed\r\n");
    } else if (starts(s, "addr", &a) && parse_num(&a, &v)) {
        oled_ok = oled_init((uint8_t)v, oled_ctl()) == 0;
        out(oled_ok ? "ok\r\n" : "init failed\r\n");
    } else if (starts(s, "speed", &a) && parse_num(&a, &v)) {
        i2c_init(v);
        oled_ok = oled_init(oled_addr(), oled_ctl()) == 0;
        out(oled_ok ? "ok\r\n" : "init failed\r\n");
    } else if (starts(s, "contrast", &a) && parse_num(&a, &v)) {
        oled_contrast((uint8_t)v);
        out("ok\r\n");
    } else if (starts(s, "invert", &a) && parse_num(&a, &v)) {
        oled_invert((int)v);
        out("ok\r\n");
    } else if (starts(s, "flip", &a) && parse_num(&a, &v)) {
        oled_flip((int)v);
        out("ok\r\n");
    } else if (starts(s, "beep", &a)) {
        if (!parse_num(&a, &v)) v = 2000;
        if (!parse_num(&a, &w)) w = 150;
        beep(v, w);
        out("ok\r\n");
    } else if (starts(s, "sound", &a) && parse_num(&a, &v)) {
        sound_on = (uint8_t)(v != 0);
        out("ok\r\n");
    } else if (starts(s, "peek", &a) && parse_num(&a, &v)) {
        if (!parse_num(&a, &w)) w = 64;
        cmd_peek(v, w);
    } else if (starts(s, "boot", &a)) {
        out("rebooting to bootloader\r\n");
        ch32v_cdc_flush();
        ch32v_cdc_reboot_to_bootloader();
    } else if (starts(s, "reset", &a)) {
        out("reset\r\n");
        ch32v_cdc_flush();
        PFIC->CFGR = PFIC_KEY3 | PFIC_SYSRST;
    } else {
        out("? (type help)\r\n");
    }
}

static void shell_poll(void) {
    int c;
    while ((c = ch32v_cdc_read()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (line_len) {
                out("\r\n");
                line[line_len] = 0;
                run_line(line);
                line_len = 0;
            }
            out("> ");
        } else if (c == 0x08 || c == 0x7F) {
            if (line_len) { line_len--; out("\b \b"); }
        } else if (c >= 0x20 && line_len < (int)sizeof(line) - 1) {
            line[line_len++] = (char)c;
            char e[2] = { (char)c, 0 };
            out(e);
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 *  Main
 * ══════════════════════════════════════════════════════════════ */
int main(void) {
    ch32v_cdc_init(NULL);
    systick_init();

    buttons_init();
    buzzer_off();

    delay_ms(50);                         /* let the OLED power up */
    i2c_init(400000);
    oled_ok = display_detect();
    term_puts("CH32V203 gameboard");
    term_puts("type 'say hello'");

    uint32_t next_frame = millis();
    uint32_t next_retry = millis();
    uint8_t  enter_long = 0;
    fps_t0 = millis();

    for (;;) {
        shell_poll();
        ch32v_cdc_poll();
        IWDG->CTLR = 0xAAAA;

        uint32_t now = millis();

        buttons_scan(now);
        for (int i = 0; i < B_COUNT; i++)
            if (btn[i].pressed) beep(btn[i].tone, 40);

        /* Long-press ENTER (0.7 s): next screen */
        if (btn[B_ENTER].down) {
            if (!enter_long && now - btn[B_ENTER].t_change > 700) {
                enter_long = 1;
                mode = (mode + 1) % MODE_COUNT;
                beep(2093, 80);
            }
        } else {
            enter_long = 0;
        }

        if (beeping && (int32_t)(now - beep_until) >= 0)
            buzzer_off();

        if (!oled_ok) {
            if ((int32_t)(now - next_retry) >= 0) {
                next_retry = now + 1000;
                oled_ok = display_detect();
            }
        } else if ((int32_t)(now - next_frame) >= 0) {
            next_frame = now + 33;
            render();
        }

        if (now - fps_t0 >= 1000) {
            fps = frames - fps_frames;
            fps_frames = frames;
            fps_t0 = now;
        }
    }
}
