import binascii
import contextlib
import io
import struct
import tempfile
import unittest
from pathlib import Path

from scripts import verify_firmware


def build_firmware(*, corrupt_crc=False, size_delta=0):
    data = bytearray(verify_firmware.HEADER_SIZE + 16)
    size = len(data) + size_delta
    header = struct.pack(
        verify_firmware.HEADER_FMT,
        verify_firmware.MAGIC,
        1,
        2,
        3,
        size,
        b"deadbeef\0\0\0\0\0\0\0\0",
        b"2026-07-12 00:00",
        0,
    )
    data[: verify_firmware.HEADER_SIZE] = header
    crc = binascii.crc32(data) & 0xFFFFFFFF
    if corrupt_crc:
        crc ^= 0xFFFFFFFF
    struct.pack_into("<I", data, verify_firmware.CRC_FIELD_OFFSET, crc)
    return bytes(data)


class VerifyFirmwareTests(unittest.TestCase):
    def verify_bytes(self, data):
        with tempfile.TemporaryDirectory() as tmp:
            firmware_path = Path(tmp) / "firmware.bin"
            firmware_path.write_bytes(data)
            with contextlib.redirect_stdout(io.StringIO()):
                return verify_firmware.verify_firmware(str(firmware_path))

    def test_valid_patched_firmware_passes(self):
        self.assertTrue(self.verify_bytes(build_firmware()))

    def test_missing_magic_fails(self):
        self.assertFalse(self.verify_bytes(b"not firmware"))

    def test_size_mismatch_fails(self):
        self.assertFalse(self.verify_bytes(build_firmware(size_delta=1)))

    def test_crc_mismatch_fails(self):
        self.assertFalse(self.verify_bytes(build_firmware(corrupt_crc=True)))

    def test_explicit_missing_signature_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            firmware_path = Path(tmp) / "firmware.bin"
            firmware_path.write_bytes(build_firmware())
            with contextlib.redirect_stdout(io.StringIO()):
                result = verify_firmware.verify_firmware(
                    str(firmware_path), str(Path(tmp) / "missing.sig")
                )
            self.assertFalse(result)


if __name__ == "__main__":
    unittest.main()
