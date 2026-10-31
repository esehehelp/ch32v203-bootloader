# CH32V203 USB CDC bootloader + ch32v-cdc

Bare-metal (no WCH HAL) USB CDC serial and a USB bootloader for the CH32V203,
ported from [ch32x035f7p6-micro-devboard](https://github.com/esehehelp/ch32x035f7p6-micro-devboard)'s `ch32x-cdc`.
After a one-time install, `pio run -t upload` reflashes over USB (no buttons, no WCH-Link),
like the Arduino 1200bps-touch flow on ESP32 / RP2040.

## Layout

```
platformio.ini        env:app (default) / env:bootloader / env:gameboard (example)
ch32_cdcboot.py       upload: 1200bps touch → CDC bootloader → write/verify/run
lib/ch32v-cdc/        USB CDC-ACM on the USBD block (PA11 D- / PA12 D+), same API as ch32x-cdc
src/startup.S         vector table (shared)
src/sections.ld       shared sections; link_app.ld / link_boot.ld set the flash window
src/boot/boot.c       bootloader, flash 0x0000-0x1FFF
src/app/main.c        your application, flash 0x2000- (sample: CDC echo)
examples/gameboard/   example app: 128x64 I2C OLED, buttons, buzzer, serial shell
```

## Usage

```bash
pio run -t upload          # build + upload src/app over USB
pio device monitor         # 115200 (the baud is ignored by USB CDC; 1200 reboots)
```

`pio run -t upload` sends a 1200bps touch to the running app, which reboots
into the bootloader (USB `1A86:FE0E`, product "Bootloader"); the image is then
erased, written in 256-byte pages, CRC32-checked and started.

If the app is broken or has no USB: **double-tap RESET** (twice within 0.5 s) →
the bootloader stays resident, then `pio run -t upload` again.

### One-time bootloader install (factory BootROM)

Hold BOOT0 high, reset (WCH ISP `4348:55E0` appears), then:

```bash
pio run -e bootloader -t upload
```

Without a valid app image the bootloader stays resident, so follow with
`pio run -t upload`.

Bootloader options (`env:bootloader` → `build_flags`):

| Flag | Default | |
|---|---|---|
| `-DBOOT_DOUBLE_TAP_MS=500` | 500 | double-tap window; `0` disables it and the start-up delay it adds |
| `-DBOOT_BUTTON_PORT=GPIOA -DBOOT_BUTTON_PIN=1` | off | recovery button, active low, held during reset |

## Writing an app

```c
#include "ch32v_regs.h"
#include "ch32v_cdc.h"

int main(void) {
    ch32v_cdc_init(NULL);              /* also sets HSI→PLL 96 MHz, USB 48 MHz */
    for (;;) {
        uint8_t buf[64];
        size_t n = ch32v_cdc_read_buf(buf, sizeof(buf));
        if (n) ch32v_cdc_write(buf, n);
        ch32v_cdc_poll();              /* handles the 1200bps reboot */
    }
}
```

API: `ch32v_cdc_available / peek / read / read_buf / write / print / flush /
connected / poll / reboot_to_bootloader`, weak `ch32v_cdc_on_rx()` callback.
Clocks: SYSCLK 96 MHz (HSI ×12), PCLK1 24 MHz, PCLK2 96 MHz, USB 48 MHz.

The app must use `src/link_app.ld` (origin 0x2000) and `src/startup.S`
(its vector entry 1 is the image marker the bootloader checks).

## Notes

- **Why not the BootROM like on the X035?** The CH32V203 has no software
  `BOOT_MODE` switch, and its factory ISP code is position-dependent (built to
  run from the 0x0 alias when BOOT0 = 1), so it cannot be entered by jumping to
  0x1FFF8000. The 1200bps touch therefore goes to this bootloader instead.
- **USBD, not USBFS:** on the V203 (D6 parts) PA11/PA12 are the USBD block;
  the X035-style USBFS driver never attaches there.
- **Other V203 parts:** defaults are for K8/C8 (64K/20K). The bootloader reads
  the flash size from the chip; RAM size and the app length are in the
  `link_*.ld` files and `board` in `platformio.ini`.
- The last 16 bytes of RAM are reserved (`_boot_magic`) in both images.
- No crystal is needed; USB runs from the factory-trimmed HSI.
