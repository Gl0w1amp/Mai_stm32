# USB Endpoint Layout

The firmware enumerates as one USB composite device. Endpoint `0` is the
standard USB control endpoint used for enumeration, descriptors, and class setup
requests. The application-facing interfaces use eight additional endpoint
addresses.

Endpoint numbers are directional. For example, `0x81 IN` and `0x01 OUT` both use
endpoint number `1`, but they are separate USB directions and serve different
functions.

## Enabled Interfaces

- CDC ACM virtual COM port
- Keyboard HID
- Custom HID button/debug input collection
- Vendor HID command collection
- Touch HID real-time touch stream

Mouse, audio, MSC, DFU, and printer classes are disabled in this firmware.

## Endpoint Table

| Endpoint | Direction | Interface | Transfer | Packet/report size | Purpose |
|:--|:--|:--|:--|:--|:--|
| `0x00` / `0x80` | OUT / IN | Control EP0 | Control | 64 bytes | USB enumeration, descriptors, and class control requests |
| `0x81` | IN | Keyboard HID | Interrupt | 14-byte report | Keyboard/button emulation reports |
| `0x82` | IN | Custom HID | Interrupt | 24-byte firmware payload, 25-byte host input report | Button bitmap, sequence, piggybacked 34-channel touch bitmap, and benchmark reports; usage page `0xFFCA`, usage `0x0001` |
| `0x83` | IN | Vendor HID Command | Interrupt | 64-byte firmware report, 65-byte Windows HID report | Command responses, usage page `0xFFCA`, usage `0x0002` |
| `0x01` | OUT | Vendor HID Command | Interrupt | 64-byte firmware report, 65-byte Windows HID report | Host command input, usage page `0xFFCA`, usage `0x0002` |
| `0x84` | IN | CDC ACM Data | Bulk | 64 bytes | Legacy/live CDC output only |
| `0x02` | OUT | CDC ACM Data | Bulk | 16 bytes | Compatibility command ingress and app-jump requests |
| `0x85` | IN | CDC ACM Notification | Interrupt | 8 bytes | CDC serial-state/control notifications |
| `0x86` | IN | Touch HID | Interrupt | 64-byte report | Real-time touch strength stream, usage page `0xFF00`, usage `0x0031` |

## Transport Rules

New host tools should send commands through the Vendor HID Command interface and
read responses from Vendor HID IN. The command frame format is identical to the
legacy CDC command frame format, but Windows HID APIs expose a leading report-ID
byte. With the current descriptor the report ID is `0`, so host buffers are
typically 65 bytes and the firmware command frame starts at host buffer offset
`1`.

CDC ACM is still present for compatibility and for legacy/live output streams.
CDC OUT accepts older command senders and app-jump requests, but command
responses are routed to Vendor HID IN. CDC IN should be treated as the legacy
touch/status stream rather than a command response channel.

## PMA Allocation Notes

The Custom HID OUT endpoint was removed, because the Custom HID collection is now
IN-only. CDC OUT was reduced to a 16-byte packet because it is kept only for
compatibility ingress. The freed PMA space is used by the Vendor HID IN/OUT
command reports.
