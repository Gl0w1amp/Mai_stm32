# Touch HID Protocol

The firmware exposes a dedicated HID collection for real-time touch strength.
This stream is independent from the legacy button Custom HID collection.

## HID Collection

| Field | Value |
|:--|:--|
| VID/PID | `AFF1:52A5` |
| Usage page | `0xFF00` |
| Usage | `0x31` |
| Report ID | `0x31` |
| Input report size | 64 bytes |
| Poll interval | 1 ms endpoint interval |
| Firmware frame period | 5 ms, 200 Hz |

Each logical touch frame contains 34 `uint16` strength values. The frame is
split into two HID reports:

- Part `0`: logical channels `0..16`
- Part `1`: logical channels `17..33`

The host should group reports by `(stream_seq, part_count)` and treat one full
touch sample as complete after receiving both parts.

## Report Layout

All multi-byte values are little-endian.

| Offset | Size | Field | Description |
|:--|:--|:--|:--|
| 0 | 1 | `report_id` | Always `0x31` |
| 1 | 1 | `protocol_version` | Current value `1` |
| 2 | 1 | `mode` | `1` = delta16 logical-order stream |
| 3 | 1 | `stream_seq` | Increments after both parts are sent |
| 4 | 1 | `part_index` | `0` or `1` |
| 5 | 1 | `part_count` | Always `2` |
| 6 | 1 | `first_logical` | `0` for part 0, `17` for part 1 |
| 7 | 1 | `value_count` | Always `17` |
| 8 | 4 | `tick_ms` | Snapshot tick in firmware milliseconds |
| 12 | 1 | `flags` | Bit0 delta, bit1 logical order, bit2 touch bits valid |
| 13 | 1 | `link_flags` | Snapshot link flags |
| 14 | 1 | `link_protocol` | PSoC/sim capsense protocol version |
| 15 | 1 | reserved | Reserved, currently `0` |
| 16 | 34 | `values` | 17 little-endian `uint16` strength values |
| 50 | 5 | `touch_bits` | 34 logical touch bits, packed LSB-first |
| 55 | 2 | `dropped_frames` | Firmware-side stale/fail drop counter |
| 57 | 4 | `snapshot_seq` | Full input snapshot sequence |
| 61 | 2 | `frame_interval_ms` | Last firmware frame interval |
| 63 | 1 | reserved | Reserved, currently `0` |

## Command Diagnostics

The normal command frame format is documented in
[Communication Commands](communication-commands.md). Send diagnostics commands
through the Vendor HID Command interface. CDC OUT remains available for older
tools, but command responses are returned through Vendor HID IN.

`GET_TOUCH_HID_STATS` uses command `0x29`.

Request payload:

- Empty payload: read stats
- `01`: reset Touch HID stats, then read stats

Response payload length is 48 bytes:

| Offset | Type | Field |
|:--|:--|:--|
| 0 | `uint32` | `frame_start_count` |
| 4 | `uint32` | `part_send_ok_count` |
| 8 | `uint32` | `busy_retry_count` |
| 12 | `uint32` | `not_ready_retry_count` |
| 16 | `uint32` | `send_fail_count` |
| 20 | `uint32` | `stale_drop_count` |
| 24 | `uint32` | `last_frame_interval_ms` |
| 28 | `uint32` | `max_frame_interval_ms` |
| 32 | `uint32` | `last_part_latency_ms` |
| 36 | `uint32` | `max_part_latency_ms` |
| 40 | `uint32` | `dropped_frames` |
| 44 | `uint8` | `pending` |
| 45 | `uint8` | `part_index` |
| 46 | `uint8` | `in_ready` |
| 47 | `uint8` | reserved |

## Host Test

On Windows:

```powershell
py scripts\touch_hid_monitor.py --list --duration 3
```

Expected healthy firmware result:

- `frame_hz` near `200`
- `packet_hz` near `400`
- `part0` and `part1` counts almost equal
- `nonzero_packets` greater than `0`
- `dropped_frames` stable

Use the Vendor HID Command interface to query or reset `GET_TOUCH_HID_STATS`
(`0x29`) when validating endpoint-level retry and drop counters.
