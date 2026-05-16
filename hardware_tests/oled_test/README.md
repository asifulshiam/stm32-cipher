# OLED Bring-Up Test

> Minimal STM32F103 project to verify the 0.96" SSD1306 OLED display over I2C, using the Lutsai SSD1306 driver library.

## Why this exists

The first SSD1306 driver I tried (a popular one based on the afiskon library) didn't work on my OLED panel — the I2C ACK was clean on a logic analyzer, but the display showed nothing. Rather than keep debugging inside the full CIPHER project (where I2C, SPI, ADC, and three timers are all live), I pulled the OLED out into a minimal project: Blue Pill + OLED + nothing else.

This isolated test confirmed two things at once:
1. The OLED hardware itself is fine
2. The **Lutsai SSD1306 driver** + **I2C Fast Mode (400 kHz)** is the combination that actually works on this hardware

Once "Hello, world!" rendered cleanly here, I migrated all OLED calls in CIPHER to the same library and bumped I2C1 to Fast Mode. Worked on the first try.

This sub-project is included so you can:
- Verify your own OLED + wiring before flashing the main CIPHER firmware
- See a minimal working example of the Lutsai driver
- Reproduce the I2C configuration that gives reliable rendering

## Hardware

| Pin | Role | Connect to |
|---|---|---|
| PB6 | I2C1 SCL | OLED **SCL** |
| PB7 | I2C1 SDA | OLED **SDA** |
| 3.3V | Power | OLED **VCC** |
| GND | Ground | OLED **GND** |

> **OLED I2C address:** Most 0.96" SSD1306 modules use `0x78` (which is `0x3C` shifted left 1). This is hardcoded in `ssd1306.h` from the Lutsai driver. If your module is `0x7A` instead, change the address there.

## Configuration

The screenshots in this folder are CubeMX exports — use them to confirm your config matches before regenerating code.

| File | Shows |
|---|---|
| `pin_configuration.png` | PB6 / PB7 assigned to I2C1 on the chip view |
| `clock_configuration.png` | HSE → PLL ×9 → 72 MHz system clock |

Key settings:
- **HSE**: Crystal/Ceramic Resonator (8 MHz)
- **PLL multiplier**: ×9 → HCLK = 72 MHz, APB1 = 36 MHz
- **I2C1 mode**: I2C
- **I2C1 speed mode**: **Fast Mode (400 kHz)** ← critical
- **SYS Debug**: Serial Wire

### Why Fast Mode matters

This is the single most important detail. With I2C1 set to **Standard Mode (100 kHz)** the bus signals look correct on a scope — START, ADDR with ACK, DATA bytes, STOP — but the OLED never renders anything. Switching to **Fast Mode (400 kHz)** in CubeMX → Connectivity → I2C1 → Parameter Settings fixes it. The exact root cause looks panel-specific (some clone SSD1306 modules are timing-sensitive at 100 kHz), but Fast Mode is the reliable answer on this hardware.

## How it works

The Lutsai driver is straightforward — it maintains a 1024-byte framebuffer in SRAM (128 × 64 ÷ 8) and pushes the whole buffer to the panel over I2C on each `SSD1306_UpdateScreen()` call.

Typical use pattern:

```c
SSD1306_Init();                                       // once, after I2C is up
SSD1306_Fill(SSD1306_COLOR_BLACK);                    // clear framebuffer
SSD1306_GotoXY(0, 0);                                 // move cursor
SSD1306_Puts("Hello, world!", &Font_7x10, 1);         // draw to framebuffer
SSD1306_UpdateScreen();                               // push framebuffer to OLED
```

The included `fonts.c` / `fonts.h` provide four bitmap fonts (6×8, 7×10, 11×18, 16×26).

## How to flash

**Option A — Mac (ST-LINK plugged directly into USB-C port, not a hub):**
```bash
/opt/ST/STM32CubeCLT_*/STM32CubeProgrammer/bin/STM32_Programmer_CLI \
  -c port=SWD -w oled_test.bin 0x08000000 -rst
```

**Option B — Windows:**
1. Install STM32CubeProgrammer (free, from st.com)
2. Connect ST-LINK V2 → Blue Pill via SWD (SWDIO, SWCLK, GND, 3.3V)
3. Open STM32CubeProgrammer → Connect → Open File → `oled_test.bin` → Download at `0x08000000`

After flashing, the OLED should immediately show test text on power-up.

## How to rebuild from source

```bash
cd src
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Output binary: `src/build/oled_test.bin`

> **CubeMX gotcha:** CubeMX regenerates `cmake/stm32cubemx/CMakeLists.txt` on every *Generate Code*, wiping the manually-added entries for `ssd1306.c` and `fonts.c`. After each regeneration, re-add them under `MX_Application_Src`.

## What to expect

On power-up the OLED should immediately display test text. The 0.96" SSD1306 panel is a two-colour OLED — the top ~16 pixels (yellow region) and the rest (blue region) are physically different colour zones on the panel, not a rendering bug. Text spanning both areas will appear in two colours; this is normal.

If the OLED stays blank:
- Confirm power: should read 3.3V on VCC, not 5V (5V is fine on most modules but 3.3V is safer for clones)
- Confirm I2C address: try `0x7A` instead of `0x78` in `ssd1306.h`
- Confirm I2C speed: must be **Fast Mode (400 kHz)** in CubeMX

## License

MIT — see the [LICENSE](../../LICENSE) file at the repo root.
