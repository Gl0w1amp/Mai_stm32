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

1. Import the existing STM32CubeIDE project and build the checked-in sources.
2. Do **not** regenerate the project from `Curva_G431_Mai.ioc`: it records the
   MCU pin/peripheral setup, but the custom HID/Vendor/Touch composite USB
   extension is maintained in checked-in code and is not fully expressible by
   the CubeMX plugin.
3. For the reproducible local path, run
   `powershell -ExecutionPolicy Bypass -File scripts/build_firmware.ps1`.
4. Flash via ST-LINK, or use the DFU jump command after the device enumerates.

## Documentation

- [USB Endpoint Layout](usb-endpoints.md)
- [Communication Commands](communication-commands.md)
- [Touch HID Protocol](touch-hid-protocol.md)

## Repository layout

- `Core/` application code and tasks
- `Drivers/` STM32 HAL/CMSIS
- `Middlewares/` USB composite and FreeRTOS sources
- `docs/` MkDocs documentation
