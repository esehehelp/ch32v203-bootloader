# Example: CH32V203K8T6 gameboard

`pio run -e gameboard -t upload` — OLED demo screens, button test, buzzer and a
serial shell (`help`) on the CDC port. Long-press ENTER cycles screens.

| Function | Pin |
|---|---|
| OLED SCL / SDA (I2C1, 0x3C, SSD1306, mounted 180°) | PB6 / PB7 |
| Buttons up / down / left / right / enter (active low) | PA10 / PA3 / PA8 / PA2 / PA1 |
| Buzzer (TIM1_CH2 PWM) | PA9 |

To make ENTER the bootloader recovery button, add
`-DBOOT_BUTTON_PORT=GPIOA -DBOOT_BUTTON_PIN=1` to `env:bootloader`.
`tools/gen_font.py` regenerates `font5x7.h`.
