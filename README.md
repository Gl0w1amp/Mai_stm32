# Mai STM32 (G431)

![Build STM32 Firmware](https://github.com/Gl0w1amp/Mai_stm32/actions/workflows/build-stm32.yml/badge.svg)
[![License: PolyForm Noncommercial 1.0.0](https://img.shields.io/badge/license-PolyForm%20Noncommercial%201.0.0-blue.svg)](LICENSE)

Firmware for an **STM32G431CBU6**-based *maimai*-style controller. It scans an
external capacitive-touch sensor board and physical buttons, drives WS2812 and
FET lighting, and presents itself to the host as a single **USB composite
device** (CDC ACM + multiple HID interfaces) so it can act as a touch/keyboard
controller and a lighting endpoint at the same time.

This repository is the **STM32G431 port** of the original STM32F411 project by
[QHPaeek](https://github.com/QHPaeek/Mai_stm32). It is built on STM32CubeIDE +
STM32 HAL, runs on FreeRTOS, and is designed to keep input latency as low as
possible while exposing a rich command and telemetry protocol.

> **Noncommercial project.** The firmware source is released under the
> [PolyForm Noncommercial License 1.0.0](LICENSE) at the original author's
> request. You may use, modify, and share it for noncommercial purposes only.
> See [License](#license).

---

## Features

- **USB composite device** built on the AL94 I-CUBE-USBD-COMPOSITE stack:
  - CDC ACM virtual COM port (legacy/live output + compatibility command ingress)
  - Keyboard HID (button emulation)
  - Custom HID button/debug input collection
  - Vendor HID command collection (primary command transport)
  - Touch HID real-time touch-strength stream (200 Hz)
- **Capacitive touch** read from an external PSoC/CY8CMBR3116-based sensor board
  over UART, with persistent per-channel thresholds and a logical→physical
  mapping table stored in flash.
- **Lighting**: 16 cascaded WS2812 LEDs (button + billboard groups) driven by
  timer + DMA, plus three PWM FET channels (body / external / side), with a
  host-controlled mode and local boot/idle/input-reactive effects.
- **`mai2led` LED-board protocol** over a dedicated UART.
- **Dual controller role** (1P / 2P) selectable at runtime; the USB PID changes
  to match (`0x52A5` / `0x52A6`).
- **IAP / DFU update support**: jump to the ST system bootloader on command, and
  a custom application bootloader with a signed firmware header.
- **Diagnostics**: VOFA/JustFloat debug streaming and UART/USB/HID statistics
  counters.

## Hardware

- **MCU**: STM32G431CBU6 (Cortex-M4F, UFQFPN48)
- **HSE**: 8 MHz (note: the F411 original used a 25 MHz HSE)
- **USB**: native USB FS device (PA11 `D-`, PA12 `D+`)

| Function | Peripheral | Pin(s) | Notes |
|:--|:--|:--|:--|
| Capacitive touch input | `UART4` | PC11 `RX`, PC10 `TX` | 230400 baud, DMA receive-to-idle, from external sensor board |
| LED board / `mai2led` | `USART1` | PA9 `TX`, PA10 `RX` | DMA, serial LED protocol |
| WS2812 button + billboard LEDs (16) | `TIM3_CH2` | PB5 | PWM + DMA bit-stream |
| FET lighting - Body | `TIM4_CH1` | PB6 | 0-255 PWM |
| FET lighting - External | `TIM4_CH2` | PB7 | 0-255 PWM |
| FET lighting - Side | `TIM4_CH4` | PB9 | 0-255 PWM |
| 8 external buttons | GPIO input | PA1-PA7, PB0 | |
| Boot button | EXTI8 | PB8 (BOOT0) | triggers runtime reset handler |
| Sensor-board reset | GPIO output | PB3 | |
| SWD debug | `SYS` | PA13 `SWDIO`, PA14 `SWCLK` | |

Additional system inputs/outputs (e.g. test/service/coin and status lines) are
assigned across PA0, PB1, PB2, PB10, PB11, PC4, PC6, and PB15. The authoritative
pin map lives in [`Curva_G431_Mai.ioc`](Curva_G431_Mai.ioc) and
[`Core/Src/gpio.c`](Core/Src/gpio.c).

The buttons and lighting are optional and the firmware boots without them, but
the touch sensor board must be connected — otherwise the firmware waits
indefinitely for touch data.

## Repository layout

| Path | Contents |
|:--|:--|
| `Core/Src`, `Core/Inc` | Application firmware, FreeRTOS tasks, drivers, USB reporting |
| `Composite/` | USB composite device descriptors/configuration |
| `Drivers/` | STM32 HAL + CMSIS (vendored, BSD-3-Clause) |
| `Middlewares/` | FreeRTOS (MIT) and AL94 USB Composite (MIT) |
| `docs/` | MkDocs documentation (protocols, USB layout) |
| `scripts/` | Build, firmware-patch/verify, simulation, and host test tools |
| `keys/` | Public keys used to verify signed firmware |
| `*.ld` | Linker scripts (application + bootloader-app layout) |
| `Curva_G431_Mai.ioc` | STM32CubeMX/CubeIDE project configuration |

### Key source modules

- `main.c`, `app_freertos.c` - startup and RTOS task setup
- `capsense_core.c`, `capsense_uart.c`, `capsense_debug.c`, `capsense_sim.c`
  - touch acquisition, detection state machine, debug streaming, and a
  simulated-touch backend
- `button.c`, `input_snapshot.c` - input scanning and snapshotting
- `LED.c` - WS2812 + FET lighting and the `mai2led` UART protocol
- `serial_commands.c`, `serial_protocol.c`, `serial_reports.c`,
  `usb_reporter.c` - command parsing, framing, and device reports
- `flash.c` - persistent settings (thresholds, mapping, role)
- `dfu_jump.c` - jump to system bootloader for DFU updates

## Building

### STM32CubeIDE (recommended)

1. Open `Curva_G431_Mai.ioc` in STM32CubeIDE (or STM32CubeMX to regenerate).
2. Make sure the **I-CUBE-USBD-COMPOSITE (AL94)** pack is installed.
3. Build the `Release` (or `Debug`) configuration. Output binaries land in
   `Release/`.

> **CubeMX regeneration note (inherited from upstream):** the AL94 composite
> pack does not always preserve user code in generated USB files. If you
> regenerate, protect your edits in the composite `App` sources (e.g.
> `usbd_cdc_acm_if.c/.h`) before regenerating.

### Windows helper script

```powershell
# Builds the Release configuration using the toolchain bundled with STM32CubeIDE
scripts\build_firmware.ps1
# Optional: build the simulated-touch variant
scripts\build_firmware.ps1 -SimTouch
```

### Command line (GNU Arm Embedded)

The CI builds with `gcc-arm-none-eabi` using a generated makefile. See
[`.github/workflows/build-stm32.yml`](.github/workflows/build-stm32.yml) for the
exact source list, include paths, and flags
(`-DUSE_HAL_DRIVER -DSTM32G431xx`, Cortex-M4F hard-float, `-Os`). Define
`CAPSENSE_SIMULATED_TOUCH_ENABLE=1` (or `make SIM_TOUCH=1`) to build the
simulated-touch firmware.

### Continuous integration

Pushes/PRs to `G431` (and `main`/`master`) build the firmware. On tags/releases
the binary is timestamped, optionally signed with OpenSSL (ECDSA P-256), and
published as a GitHub Release artifact and to Cloudflare R2 with a `manifest.json`.

## Flashing

- **DFU**: put the device in DFU mode (BOOT0, or send the `JUMP_TO_DFU` command),
  then flash the `.bin` with STM32CubeProgrammer over USB DFU.
- **ST-LINK / SWD**: flash the `.elf`/`.bin` via SWD (PA13/PA14).
- **IAP**: the application bootloader can accept a new image and validates the
  signed firmware header before jumping to the application.

After flashing, replug the device and assign the enumerated COM/HID interfaces as
needed (the controller role determines the USB PID; touch/LED/IO live on separate
interfaces of the composite device).

## Documentation

The `docs/` folder is an [MkDocs](https://www.mkdocs.org/) (Material theme) site:

- [USB Endpoint Layout](docs/usb-endpoints.md) - composite interfaces and endpoints
- [Communication Commands](docs/communication-commands.md) - binary, legacy ASCII,
  and LED UART command protocols
- [Touch HID Protocol](docs/touch-hid-protocol.md) - real-time touch-strength stream

Build the docs locally with `mkdocs serve` (see [`mkdocs.yml`](mkdocs.yml)).

## Host tools

`scripts/` includes Python utilities for development and validation, e.g.:

- `touch_hid_monitor.py` - monitor the Touch HID stream and report frame/packet rates
- `simulate_capsense.py`, `capsense_gui_playground.py`,
  `plot_capsense_*.py`, `plot_vofa_capture_segment.py` - touch-algorithm
  simulation and analysis
- `patch_firmware.py`, `verify_firmware.py` - firmware header patching/verification

```powershell
py scripts\touch_hid_monitor.py --list --duration 3
```

## Acknowledgements

- **Original project** (STM32F411): [QHPaeek/Mai_stm32](https://github.com/QHPaeek/Mai_stm32)
- **Serial JVS/IO companion**: [QHPaeek/Affine_IO](https://github.com/QHPaeek/Affine_IO)
- **Touch sensor module**: [CY8CMBR3116 touch module (OSHWHub)](https://oshwhub.com/affinelab/cy8cmbr3116_touch_module)
- **USB composite stack**: [alambe94/I-CUBE-USBD-Composite](https://github.com/alambe94/I-CUBE-USBD-Composite)

## License

This project's own source code is licensed under the
**[PolyForm Noncommercial License 1.0.0](LICENSE)**
(`SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`).

You may use, modify, and distribute it for **noncommercial purposes only**
(personal use, hobby projects, research, education, etc.). **Commercial use is
not permitted.** This restriction is applied at the request of the original
author.

Third-party components under `Drivers/` and `Middlewares/` are **not** covered by
this license and remain under their respective original licenses (STM32 HAL/CMSIS:
BSD-3-Clause; FreeRTOS: MIT; AL94 USB Composite: MIT).

> *Required Notice: Copyright (c) 2026 QHPaeek and Gl0w1amp — https://github.com/Gl0w1amp/Mai_stm32*
