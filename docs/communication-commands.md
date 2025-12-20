# Communication Commands

This page documents the USB CDC command protocol implemented by the firmware.
The device exposes a USB CDC ACM interface (Virtual COM Port). Commands are sent to the device, and the device replies with responses or streams data.

## Protocol Overview

- **Interface**: USB CDC ACM (Channel 0)
- **Baud Rate**: Any (USB virtual serial port)
- **Endianness**: Little-endian for multi-byte values
- **Direction**:
    - Host -> Device: Commands
    - Device -> Host: Responses, Touch Reports

The firmware supports two command formats:
1.  **Binary Protocol**: Frames starting with `0xFF`. Used for all modern features.
2.  **Legacy ASCII Protocol**: Frames starting with `{` (`0x7B`). Used for basic compatibility with some tools.

---

## Binary Protocol (0xFF)

### Frame Format

All binary commands follow this structure:

| Offset | Field    | Size | Description                                      |
|:-------|:---------|:-----|:-------------------------------------------------|
| 0      | Start    | 1    | Fixed `0xFF`                                     |
| 1      | Cmd      | 1    | Command ID                                       |
| 2      | Len      | 1    | Length of the Payload                            |
| 3      | Payload  | Len  | Command-specific data                            |
| 3+Len  | Checksum | 1    | Sum of bytes `0` to `2+Len` (modulo 256)         |

### LED Commands

Control the on-board LEDs and external lighting.

| Cmd  | Name | Payload | Description |
|:-----|:-----|:--------|:------------|
| `0x02` | `LED` | *Reserved* | Reserved for future use. |
| `0x14` | `LED_BUTTON` | 24 bytes + 1 byte (optional) | Update Button LEDs (8 LEDs).<br>**Payload**: 24 bytes RGB data (8 x 3 bytes) + 1 byte Speed (optional).<br>If Speed is provided, LEDs fade to target color. |
| `0x15` | `LED_BILLBOARD` | 24 bytes | Update Billboard LEDs.<br>**Payload**: 24 bytes RGB data. |
| `0x16` | `LED_PWM` | 3 bytes | Control PWM channels (FETs).<br>**Payload**: `[BodyLed, ExtLed, SideLed]` (0-255). |

**Notes:**
- **RGB Data**: 3 bytes per LED in R, G, B order.
- **Speed**: 0 = Immediate, >0 = Fade duration (lower is slower, calculation: `4095 / speed * 8` ticks).

### Touch & Settings Commands

Configure touch sensor parameters and read raw data.

| Cmd  | Name | Payload | Response | Description |
|:-----|:-----|:--------|:---------|:------------|
| `0x03` | `SCAN_START` | None | None | Start touch scanning (internal flag). |
| `0x04` | `SCAN_STOP` | None | None | Stop touch scanning. |
| `0x05` | `READ_MONO_THRESHOLD` | 1 byte (`idx`) | `FF 05 03 [idx] [val_L] [val_H] [CS]` | Read threshold for sensor `idx` (0-33). |
| `0x06` | `WRITE_MONO_THRESHOLD` | 3 bytes (`idx`, `val_L`, `val_H`) | `FF 06 01 01 [CS]` | Write threshold for sensor `idx`. |
| `0x07` | `READ_TOUCH_SHEET` | None | `FF 07 22 [34 bytes] [CS]` | Read all 34 sensor raw values. |
| `0x08` | `WRITE_TOUCH_SHEET` | 34 bytes | `FF 08 01 01 [CS]` | Write all 34 sensor raw values. |
| `0x12` | `READ_DELAY_SETTING` | 1 byte (`idx`) | `FF 12 02 [idx] [val] [CS]` | Read delay setting `idx` (0-1). |
| `0x13` | `WRITE_DELAY_SETTING` | 2 bytes (`idx`, `val`) | `FF 13 01 [idx] [CS]` | Write delay setting `idx`. |

### System Commands

System management and information.

| Cmd  | Name | Payload | Response | Description |
|:-----|:-----|:--------|:---------|:------------|
| `0x10` | `RESET` | None | `FF 10 01 01 [CS]` | Software reset (MCU reboot). |
| `0x11` | `HEART_BEAT` | None | None | Reset the "Heartbeat" timer. Keeps the connection alive. |
| `0x21` | `JUMP_TO_DFU` | None | `FF 21 01 01 [CS]` | Jump to System Bootloader (DFU mode). |
| `0xF0` | `GET_BOARD_INFO` | None | `FF F0 [Len] [VerLen] [Ver...] [NameLen] [Name...] [UIDLen] [UID...] [CS]` | Get firmware version, board name, and MCU UID. |

**Board Info Response Format:**
- `Version Length` (1 byte) + `Version String`
- `Name Length` (1 byte) + `Name String` (e.g., "1020-050201")
- `UID Length` (1 byte) + `UID Bytes` (12 bytes)

---

## Device Reports (Device -> Host)

The device sends data to the host automatically based on state.

### Auto Scan Report (0x01)
Sent periodically when `Heartbeat > 0`.

**Frame Size**: 14 bytes
**Format**:
`FF 01 0A [Btn0_L] [Btn0_H] [Btn1] [Touch0] ... [Touch6] 0A`

- **Btn0_L**: `button_status[0] & 0x0F`
- **Btn0_H**: `button_status[0] & 0xF0`
- **Btn1**: `button_status[1]`
- **Touch0..6**: 7 bytes containing 34 touch sensor bits (packed 5 bits per byte).

### Touch Scan Frame (0x28)
Sent when `Heartbeat == 0` and `Touch Scan Flag` is enabled (via `{STAT}`).

**Frame Size**: 9 bytes
**Format**:
`28 [Touch0] ... [Touch6] 29`

- **Start**: `0x28` (`(`)
- **Touch Data**: 7 bytes (same packing as Auto Scan)
- **End**: `0x29` (`)`)

---

## Legacy ASCII Protocol (0x7B)

These commands start with `{` (`0x7B`) and are 6 bytes long. They are primarily for compatibility with Aime-compatible readers/tools.

| Command | Description | Response |
|:--------|:------------|:---------|
| `{STAT}` | Enable Touch Scan reporting (sets `touch_scan_flag = 1`). | None |
| `{HALT}` | Disable Touch Scan reporting (sets `touch_scan_flag = 0`). | None |
| `{RSET}` | No-op (Reset placeholder). | `(RSET)` |
| `{R...}` | Player 2 commands (e.g., `{Rrat}`, `{Rsen}`).<br>Currently placeholders, echoes command. | `(R...)` |
| `{L...}` | Player 1 commands (e.g., `{Lrat}`, `{Lsen}`).<br>Currently placeholders, echoes command. | `(L...)` |

**Note**: The `{R...}` and `{L...}` commands are intended for setting Ratio and Sensitivity but are currently not fully implemented (they update the `player` variable but do not apply settings).

---

## LED Controller Protocol (UART)

In addition to the USB CDC commands, the firmware implements a dedicated LED control protocol on the UART interface (typically for internal communication or specific LED controllers).

### Frame Format

| Offset | Field | Size | Description |
|:---|:---|:---|:---|
| 0 | Sync | 1 | `0xE0` |
| 1 | DstNodeID | 1 | Destination Node ID |
| 2 | SrcNodeID | 1 | Source Node ID |
| 3 | Length | 1 | Length of Payload (Cmd + Data) |
| 4 | Cmd | 1 | Command ID |
| 5 | Data | Var | Command Data |
| 5+N | Checksum | 1 | Sum of bytes `DstNodeID` to end of `Data` |

**Escaping**:
If any byte in the frame (except Sync) is `0xE0` or `0xD0`, it is escaped:
- `0xE0` -> `0xD0 0xDF`
- `0xD0` -> `0xD0 0xCF`

### Commands

| Cmd | Name | Payload | Description |
|:---|:---|:---|:---|
| `0x31` | `SetLedGs8Bit` | `[Idx] [R] [G] [B]` | Set LED `Idx` to color immediately. |
| `0x32` | `SetLedGs8BitMulti` | `[Start] [Count] [Skip] [R] [G] [B]` | Set a range of LEDs to the same color immediately. |
| `0x33` | `SetLedGs8BitMultiFade` | `[Start] [Count] [Skip] [R] [G] [B] [Speed]` | Fade a range of LEDs to color.<br>**Speed**: `4095 / speed * 8` ticks. |
| `0x39` | `SetLedFet` | `[Body] [Ext] [Side]` | Control PWM FETs (0-255). |
| `0x3C` | `SetLedGsUpdate` | None | Apply pending fades and refresh LEDs. |
