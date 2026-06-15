#!/usr/bin/env python3
"""Monitor the Curva Mai touch-strength HID stream on Windows."""

from __future__ import annotations

import argparse
import ctypes
import struct
import sys
import time
from ctypes import wintypes


VID_DEFAULT = 0xAFF1
PID_DEFAULT = 0x52A5
TOUCH_USAGE_PAGE = 0xFF00
TOUCH_USAGE = 0x31
REPORT_ID = 0x31
SERIAL_CMD_GET_CAPSENSE_UART_STATS = 0x18
SERIAL_CMD_GET_TOUCH_HID_STATS = 0x29


def make_serial_frame(command: int, payload: bytes = b"") -> bytes:
    frame = bytearray([0xFF, command & 0xFF, len(payload) & 0xFF])
    frame.extend(payload)
    frame.append(sum(frame) & 0xFF)
    return bytes(frame)


def read_serial_frame(ser, wanted_command: int, timeout_s: float = 1.0) -> bytes | None:
    deadline = time.time() + timeout_s
    buf = bytearray()
    while time.time() < deadline:
        buf.extend(ser.read(128))
        while len(buf) >= 4:
            while buf and buf[0] != 0xFF:
                del buf[0]
            if len(buf) < 4:
                break
            length = buf[2] + 4
            if length < 4 or length > 128:
                del buf[0]
                continue
            if len(buf) < length:
                break
            frame = bytes(buf[:length])
            del buf[:length]
            if (sum(frame[:-1]) & 0xFF) == frame[-1] and frame[1] == wanted_command:
                return frame
        time.sleep(0.002)
    return None


def query_cdc_stats(port: str, reset: bool) -> None:
    try:
        import serial
    except ImportError:
        print("cdc: pyserial is not installed")
        return

    with serial.Serial(port, 115200, timeout=0.02, write_timeout=1) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        payload = b"\x01" if reset else b""
        ser.write(make_serial_frame(SERIAL_CMD_GET_TOUCH_HID_STATS, payload))
        ser.flush()
        touch_stats = read_serial_frame(ser, SERIAL_CMD_GET_TOUCH_HID_STATS)
        ser.write(make_serial_frame(SERIAL_CMD_GET_CAPSENSE_UART_STATS))
        ser.flush()
        capsense_stats = read_serial_frame(ser, SERIAL_CMD_GET_CAPSENSE_UART_STATS)

    if touch_stats:
        p = touch_stats[3:-1]
        values = struct.unpack_from("<11I4B", p)
        keys = (
            "frame_start",
            "part_send_ok",
            "busy_retry",
            "not_ready_retry",
            "send_fail",
            "stale_drop",
            "last_frame_interval_ms",
            "max_frame_interval_ms",
            "last_part_latency_ms",
            "max_part_latency_ms",
            "dropped_frames",
            "pending",
            "part_index",
            "in_ready",
            "reserved",
        )
        print("touch_hid_stats:", dict(zip(keys, values)))
    else:
        print("touch_hid_stats: no response")

    if capsense_stats:
        p = capsense_stats[3:-1]
        counts = struct.unpack_from("<8I", p)
        print(
            "capsense_uart_stats:",
            {
                "checksum_accept": counts[0],
                "rolling_accept": counts[1],
                "legacy_accept": counts[2],
                "protocol_version": p[32],
                "legacy_offset": p[33],
                "failure_streak": p[34],
            },
        )
    else:
        print("capsense_uart_stats: no response")


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_ubyte * 8),
    ]


class DeviceInterfaceData(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("InterfaceClassGuid", GUID),
        ("Flags", wintypes.DWORD),
        ("Reserved", ctypes.c_void_p),
    ]


class DeviceInterfaceDetailData(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("DevicePath", wintypes.WCHAR * 1024)]


class HidAttributes(ctypes.Structure):
    _fields_ = [
        ("Size", wintypes.ULONG),
        ("VendorID", wintypes.USHORT),
        ("ProductID", wintypes.USHORT),
        ("VersionNumber", wintypes.USHORT),
    ]


class HidCaps(ctypes.Structure):
    _fields_ = [
        ("Usage", wintypes.USHORT),
        ("UsagePage", wintypes.USHORT),
        ("InputReportByteLength", wintypes.USHORT),
        ("OutputReportByteLength", wintypes.USHORT),
        ("FeatureReportByteLength", wintypes.USHORT),
        ("Reserved", wintypes.USHORT * 17),
        ("NumberLinkCollectionNodes", wintypes.USHORT),
        ("NumberInputButtonCaps", wintypes.USHORT),
        ("NumberInputValueCaps", wintypes.USHORT),
        ("NumberInputDataIndices", wintypes.USHORT),
        ("NumberOutputButtonCaps", wintypes.USHORT),
        ("NumberOutputValueCaps", wintypes.USHORT),
        ("NumberOutputDataIndices", wintypes.USHORT),
        ("NumberFeatureButtonCaps", wintypes.USHORT),
        ("NumberFeatureValueCaps", wintypes.USHORT),
        ("NumberFeatureDataIndices", wintypes.USHORT),
    ]


class Overlapped(ctypes.Structure):
    _fields_ = [
        ("Internal", ctypes.c_void_p),
        ("InternalHigh", ctypes.c_void_p),
        ("Offset", wintypes.DWORD),
        ("OffsetHigh", wintypes.DWORD),
        ("hEvent", wintypes.HANDLE),
    ]


class HidApi:
    DIGCF_PRESENT = 0x02
    DIGCF_DEVICEINTERFACE = 0x10
    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    FILE_SHARE_READ = 0x01
    FILE_SHARE_WRITE = 0x02
    OPEN_EXISTING = 3
    FILE_FLAG_OVERLAPPED = 0x40000000
    ERROR_IO_PENDING = 997
    WAIT_OBJECT_0 = 0
    INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
    HIDP_STATUS_SUCCESS = 0x110000

    def __init__(self) -> None:
        self.hid = ctypes.WinDLL("hid", use_last_error=True)
        self.setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._configure_types()

    def _configure_types(self) -> None:
        self.hid.HidD_GetHidGuid.argtypes = [ctypes.POINTER(GUID)]
        self.hid.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.POINTER(HidAttributes)]
        self.hid.HidD_GetPreparsedData.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
        self.hid.HidD_FreePreparsedData.argtypes = [ctypes.c_void_p]
        self.hid.HidP_GetCaps.argtypes = [ctypes.c_void_p, ctypes.POINTER(HidCaps)]
        self.hid.HidP_GetCaps.restype = wintypes.LONG

        self.setupapi.SetupDiGetClassDevsW.argtypes = [
            ctypes.POINTER(GUID),
            wintypes.LPCWSTR,
            wintypes.HWND,
            wintypes.DWORD,
        ]
        self.setupapi.SetupDiGetClassDevsW.restype = ctypes.c_void_p
        self.setupapi.SetupDiEnumDeviceInterfaces.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(GUID),
            wintypes.DWORD,
            ctypes.POINTER(DeviceInterfaceData),
        ]
        self.setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(DeviceInterfaceData),
            ctypes.c_void_p,
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
            ctypes.c_void_p,
        ]
        self.setupapi.SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]

        self.kernel32.CreateFileW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            ctypes.c_void_p,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        ]
        self.kernel32.CreateFileW.restype = wintypes.HANDLE
        self.kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel32.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
        self.kernel32.CreateEventW.restype = wintypes.HANDLE
        self.kernel32.ReadFile.argtypes = [
            wintypes.HANDLE,
            ctypes.c_void_p,
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
            ctypes.POINTER(Overlapped),
        ]
        self.kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel32.GetOverlappedResult.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(Overlapped),
            ctypes.POINTER(wintypes.DWORD),
            wintypes.BOOL,
        ]
        self.kernel32.CancelIo.argtypes = [wintypes.HANDLE]

    def open(self, path: str, overlapped: bool = False):
        flags = self.FILE_FLAG_OVERLAPPED if overlapped else 0
        return self.kernel32.CreateFileW(
            path,
            self.GENERIC_READ | self.GENERIC_WRITE,
            self.FILE_SHARE_READ | self.FILE_SHARE_WRITE,
            None,
            self.OPEN_EXISTING,
            flags,
            None,
        )

    def close(self, handle) -> None:
        if handle and handle != self.INVALID_HANDLE_VALUE:
            self.kernel32.CloseHandle(handle)

    def get_caps(self, handle):
        attrs = HidAttributes()
        attrs.Size = ctypes.sizeof(attrs)
        if not self.hid.HidD_GetAttributes(handle, ctypes.byref(attrs)):
            return None

        preparsed = ctypes.c_void_p()
        caps = HidCaps()
        if not self.hid.HidD_GetPreparsedData(handle, ctypes.byref(preparsed)):
            return None
        try:
            status = self.hid.HidP_GetCaps(preparsed, ctypes.byref(caps))
        finally:
            self.hid.HidD_FreePreparsedData(preparsed)
        if status != self.HIDP_STATUS_SUCCESS:
            return None
        return attrs, caps

    def enumerate_hid(self, vid: int, pid: int):
        guid = GUID()
        self.hid.HidD_GetHidGuid(ctypes.byref(guid))
        info = self.setupapi.SetupDiGetClassDevsW(
            ctypes.byref(guid), None, None, self.DIGCF_PRESENT | self.DIGCF_DEVICEINTERFACE
        )
        matches = []
        index = 0
        try:
            while True:
                iface = DeviceInterfaceData()
                iface.cbSize = ctypes.sizeof(iface)
                if not self.setupapi.SetupDiEnumDeviceInterfaces(info, None, ctypes.byref(guid), index, ctypes.byref(iface)):
                    break
                detail = DeviceInterfaceDetailData()
                detail.cbSize = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
                needed = wintypes.DWORD()
                ok = self.setupapi.SetupDiGetDeviceInterfaceDetailW(
                    info, ctypes.byref(iface), ctypes.byref(detail), ctypes.sizeof(detail), ctypes.byref(needed), None
                )
                if ok:
                    path = detail.DevicePath
                    needle = f"vid_{vid:04x}&pid_{pid:04x}"
                    if needle in path.lower():
                        handle = self.open(path)
                        try:
                            caps = self.get_caps(handle) if handle and handle != self.INVALID_HANDLE_VALUE else None
                        finally:
                            self.close(handle)
                        if caps:
                            attrs, hid_caps = caps
                            matches.append((path, attrs, hid_caps))
                index += 1
        finally:
            self.setupapi.SetupDiDestroyDeviceInfoList(info)
        return matches

    def read_one(self, handle, report_len: int, timeout_ms: int):
        buf = (ctypes.c_ubyte * report_len)()
        got = wintypes.DWORD(0)
        event = self.kernel32.CreateEventW(None, True, False, None)
        ov = Overlapped()
        ov.hEvent = event
        try:
            ok = self.kernel32.ReadFile(handle, buf, report_len, ctypes.byref(got), ctypes.byref(ov))
            if not ok:
                err = ctypes.get_last_error()
                if err != self.ERROR_IO_PENDING:
                    return None
                wait = self.kernel32.WaitForSingleObject(event, timeout_ms)
                if wait != self.WAIT_OBJECT_0:
                    self.kernel32.CancelIo(handle)
                    return None
                if not self.kernel32.GetOverlappedResult(handle, ctypes.byref(ov), ctypes.byref(got), False):
                    return None
            return bytes(buf[: got.value])
        finally:
            self.kernel32.CloseHandle(event)


def parse_touch_report(report: bytes) -> dict:
    values = [report[16 + i * 2] | (report[17 + i * 2] << 8) for i in range(17)]
    return {
        "report_id": report[0],
        "version": report[1],
        "mode": report[2],
        "stream_seq": report[3],
        "part": report[4],
        "part_count": report[5],
        "first_logical": report[6],
        "value_count": report[7],
        "tick_ms": struct.unpack_from("<I", report, 8)[0],
        "flags": report[12],
        "link_flags": report[13],
        "link_protocol": report[14],
        "values": values,
        "touch_bits": report[50:55],
        "dropped_frames": report[55] | (report[56] << 8),
        "snapshot_seq": struct.unpack_from("<I", report, 57)[0],
        "frame_interval_ms": report[61] | (report[62] << 8),
    }


def monitor(args: argparse.Namespace) -> int:
    api = HidApi()
    devices = api.enumerate_hid(args.vid, args.pid)
    if args.list:
        print(f"hid_collections: {len(devices)}")
        for index, (path, attrs, caps) in enumerate(devices):
            print(
                index,
                f"vid={attrs.VendorID:04x} pid={attrs.ProductID:04x} "
                f"page=0x{caps.UsagePage:04x} usage=0x{caps.Usage:04x} "
                f"in={caps.InputReportByteLength} out={caps.OutputReportByteLength}",
                path,
            )

    touch = [(path, caps) for path, _attrs, caps in devices if caps.UsagePage == TOUCH_USAGE_PAGE and caps.Usage == TOUCH_USAGE]
    if not touch:
        print("touch_hid: not found")
        return 2

    path, caps = touch[0]
    report_len = caps.InputReportByteLength
    handle = api.open(path, overlapped=True)
    if not handle or handle == api.INVALID_HANDLE_VALUE:
        print(f"touch_hid: open failed, error={ctypes.get_last_error()}")
        return 3

    packets = []
    deadline = time.perf_counter() + args.duration
    try:
        while time.perf_counter() < deadline:
            report = api.read_one(handle, report_len, args.timeout_ms)
            if report:
                packets.append((time.perf_counter(), report))
    finally:
        api.close(handle)

    parsed = [parse_touch_report(report) for _ts, report in packets if len(report) >= 64 and report[0] == REPORT_ID]
    part0 = [row for row in parsed if row["part"] == 0]
    part1 = [row for row in parsed if row["part"] == 1]
    nonzero = [row for row in parsed if any(row["values"]) or any(row["touch_bits"])]
    intervals = [
        (packets[i][0] - packets[i - 1][0]) * 1000.0
        for i in range(1, len(packets))
        if packets[i][1][4] == 0 and packets[i - 1][1][4] == 1
    ]

    print(
        "touch_hid_capture:",
        {
            "valid_packets": len(parsed),
            "part0": len(part0),
            "part1": len(part1),
            "frame_hz": round(len(part0) / args.duration, 2) if args.duration else 0,
            "packet_hz": round(len(parsed) / args.duration, 2) if args.duration else 0,
            "nonzero_packets": len(nonzero),
            "drop_first_last": (
                parsed[0]["dropped_frames"],
                parsed[-1]["dropped_frames"],
            )
            if parsed
            else None,
            "part_boundary_interval_avg_ms": round(sum(intervals) / len(intervals), 3) if intervals else None,
        },
    )

    for row in nonzero[: args.samples]:
        print(
            "sample:",
            {
                "seq": row["stream_seq"],
                "part": row["part"],
                "first_logical": row["first_logical"],
                "tick_ms": row["tick_ms"],
                "snapshot_seq": row["snapshot_seq"],
                "link_flags": row["link_flags"],
                "link_protocol": row["link_protocol"],
                "frame_interval_ms": row["frame_interval_ms"],
                "values0_8": row["values"][:9],
                "touch_bits": row["touch_bits"].hex(" "),
                "dropped_frames": row["dropped_frames"],
            },
        )

    if args.cdc:
        query_cdc_stats(args.cdc, args.reset_stats)

    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=VID_DEFAULT)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=PID_DEFAULT)
    parser.add_argument("--duration", type=float, default=3.0)
    parser.add_argument("--timeout-ms", type=int, default=300)
    parser.add_argument("--samples", type=int, default=4)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--cdc", help="Optional CDC COM port, for example COM8")
    parser.add_argument("--reset-stats", action="store_true")
    args = parser.parse_args(argv)
    return monitor(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
