# HC-SR04 Bring-Up Test

> Minimal STM32F103 project to verify the HC-SR04 ultrasonic sensor in isolation, before integrating it into the main CIPHER game loop.

## Why this exists

While building CIPHER, the HC-SR04 readings in Stage 3 were unstable. Rather than debug inside the main project (where five other peripherals share the bus and IRQ table), I peeled the sensor out into a standalone project — just the Blue Pill, the sensor, and the minimum code to read a distance. That isolated the problem cleanly: the sensor was fine all along; the bug was in the main project's `TIM1` Input Capture configuration. The fix that came out of this test (DWT GPIO polling) was then ported back to CIPHER Stage 3.

This sub-project is included here so you can:
- Verify your own HC-SR04 wiring without flashing the full CIPHER firmware
- Re-use the GPIO polling approach in your own projects
- Reproduce the exact configuration that gave clean readings up to 30 cm

## Hardware

| Pin | Role | Connect to |
|---|---|---|
| PA9 | TRIG (GPIO output) | HC-SR04 **TRIG** |
| PA8 | ECHO (GPIO input, 5V-tolerant) | HC-SR04 **ECHO** |
| 5V | Power | HC-SR04 **VCC** (must be 5V — 3.3V will not work) |
| GND | Ground | HC-SR04 **GND** |

> **Power note:** The 5V pin on Blue Pill is only live when the board is powered through its micro-USB port (or from a 5V source on the 5V pin itself), not when powered from the ST-LINK 3.3V. Flash with ST-LINK, then disconnect and run on USB power for testing.

## Configuration

The screenshots in this folder are exports from STM32CubeMX — use them to confirm your config matches before regenerating code.

| File | Shows |
|---|---|
| `pin_configuration.png` | The pin assignments on the Blue Pill chip view |
| `clock_configuration.png` | HSE → PLL ×9 → 72 MHz system clock |

Key settings:
- **HSE**: Crystal/Ceramic Resonator (8 MHz)
- **PLL multiplier**: ×9 → HCLK = 72 MHz
- **TIM1**: Internal Clock only, Prescaler = 71 (gives 1 µs tick at 72 MHz)
- **PA8**: GPIO_Input, no pull
- **PA9**: GPIO_Output, no pull, low-speed
- **SYS Debug**: Serial Wire (required for SWD flashing)

## How it works

The approach is deliberately simple — no input capture, no DMA, no IRQ. Just a timer running at 1 µs/tick and two GPIO polls:

1. Pull TRIG high for 10 µs, then low — this fires the sensor's ultrasonic burst
2. Wait for ECHO to go high (sensor detected the return), capture timer counter → `V1`
3. Wait for ECHO to go low (echo pulse ends), capture timer counter → `V2`
4. Elapsed time = `V2 − V1` (with wraparound handling if `V2 < V1`)
5. Distance in cm = elapsed µs / 58

Wraparound matters because the 16-bit timer overflows at 65535 µs. If `V2 < V1`, the timer has wrapped, and the correct elapsed value is `(65535 − V1) + V2`.

Verified accuracy: 4 cm reads as 4.2 cm, 10 cm reads as 10.1 cm, 30 cm reads as 30.3 cm — all within the sensor's ±0.3 cm spec.

## How to flash

**Option A — Mac (ST-LINK plugged directly into USB-C port, not a hub):**
```bash
/opt/ST/STM32CubeCLT_*/STM32CubeProgrammer/bin/STM32_Programmer_CLI \
  -c port=SWD -w hcsr04_test.bin 0x08000000 -rst
```

**Option B — Windows:**
1. Install STM32CubeProgrammer (free, from st.com)
2. Connect ST-LINK V2 → Blue Pill via SWD (SWDIO, SWCLK, GND, 3.3V)
3. Open STM32CubeProgrammer → Connect → Open File → `hcsr04_test.bin` → Download at `0x08000000`

After flashing: disconnect ST-LINK, connect Blue Pill via micro-USB for testing (so 5V is available for the sensor).

## How to rebuild from source

```bash
cd src
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Output binary: `src/build/hcsr04_test.bin`

> **CubeMX gotcha:** Each time you click *Generate Code* in CubeMX, the `htim1.Init.Prescaler` value gets reset to `0`. You must manually set it to `71` in `Core/Src/tim.c` after every regeneration, or all readings will be ~70× too large.

## What to expect

With the sensor pointed at a wall ~10 cm away and a serial debugger attached, you should see `dist ≈ 10` in the timer trace variable. The main loop runs continuously, so moving the sensor closer/farther updates the reading in real time.

If readings are stuck at `999`, the sensor isn't echoing — check 5V supply, ground connection, and the ECHO wire.

## License

MIT — see the [LICENSE](../../LICENSE) file at the repo root.
