# Mai STM32 Firmware

Mai STM32 is firmware for an STM32G431-based controller that scans capacitive
touch and buttons, drives LED outputs, and exposes USB CDC + HID interfaces.

## What this repo contains

- Core firmware source and FreeRTOS tasks
- USB composite device stack (CDC ACM + HID)
- LED control (WS2812 data and FET PWM)
- Touch and button scanning with persistent settings in flash
- DFU jump support for firmware updates

## Quick start

1. Open `Curva_G431_Mai.ioc` in STM32CubeIDE or STM32CubeMX.
2. Generate code if needed, then build using the included project files.
3. Flash via ST-LINK, or use the DFU jump command after the device enumerates.

## Documentation

- [Communication Commands](communication-commands.md)

## Repository layout

- `Core/` application code and tasks
- `Drivers/` STM32 HAL/CMSIS
- `Middlewares/` USB composite and FreeRTOS sources
- `docs/` MkDocs documentation
