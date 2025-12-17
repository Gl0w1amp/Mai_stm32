import argparse
import binascii
import os
import struct
import subprocess
import sys
from pathlib import Path
import shutil

MAGIC = b"\x41\x46\x46\x49"
HEADER_FMT = "<4sIIII16s16sI"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
CRC_FIELD_OFFSET = 52


def resolve_signature_path(firmware_path, signature_arg):
    if signature_arg:
        return signature_arg

    candidates = [f"{firmware_path}.sig", f"{firmware_path}.dev.sig"]
    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate

    return None


def resolve_pubkey_path(sig_path, pubkey_arg):
    if pubkey_arg:
        return pubkey_arg

    if not sig_path:
        return None

    sig_lower = str(sig_path).lower()
    keys_dir = Path(__file__).resolve().parent.parent / "keys"

    if sig_lower.endswith(".dev.sig"):
        return str(keys_dir / "dev_fw_pub.pem")
    if sig_lower.endswith(".sig"):
        return str(keys_dir / "rel_fw_pub.pem")

    return None


def verify_signature(firmware_path, sig_path, pubkey_path, sig_requested):
    if sig_path is None:
        if sig_requested:
            print("Signature Check: FAILED (signature file not found)")
            return False
        print("Signature Check: SKIPPED (signature file not found)")
        return None

    if not os.path.exists(sig_path):
        print(f"Signature Check: FAILED (signature file not found: {sig_path})")
        return False

    if not pubkey_path:
        print("Signature Check: FAILED (public key not resolved, pass --pubkey)")
        return False

    if not os.path.exists(pubkey_path):
        print(f"Signature Check: FAILED (public key not found: {pubkey_path})")
        return False

    openssl_path = shutil.which("openssl")
    if not openssl_path:
        print("Signature Check: SKIPPED (openssl not found on PATH)")
        return None

    print(f"Signature File: {sig_path}")
    print(f"Public Key:     {pubkey_path}")

    result = subprocess.run(
        [
            openssl_path,
            "dgst",
            "-sha256",
            "-verify",
            pubkey_path,
            "-signature",
            sig_path,
            firmware_path,
        ],
        capture_output=True,
        text=True,
    )

    if result.returncode == 0:
        print("Signature Check: PASSED")
        return True

    print("Signature Check: FAILED")
    if result.stdout.strip():
        print(f"OpenSSL Output: {result.stdout.strip()}")
    if result.stderr.strip():
        print(f"OpenSSL Error:  {result.stderr.strip()}")
    return False


def verify_firmware(file_path, signature_path=None, pubkey_path=None):
    if not os.path.exists(file_path):
        print(f"Error: File '{file_path}' not found.")
        sys.exit(1)

    print(f"Verifying firmware: {file_path}")
    with open(file_path, "rb") as f:
        data = f.read()

    offset = data.find(MAGIC)
    if offset == -1:
        print("Verification Failed: Magic 'AFFI' not found!")
        return

    print(f"Found Header at offset: 0x{offset:X}")

    if offset + HEADER_SIZE > len(data):
        print("Parse Error: Header is truncated.")
        return

    try:
        header_data = data[offset : offset + HEADER_SIZE]
        magic_val, major, minor, patch, size, git_hash, time, crc = struct.unpack(
            HEADER_FMT, header_data
        )
    except struct.error as exc:
        print(f"Parse Error: {exc}")
        return

    if magic_val != MAGIC:
        print("Parse Error: Header magic mismatch.")
        return

    git_hash_str = git_hash.decode("utf-8", errors="ignore").strip("\x00")
    time_str = time.decode("utf-8", errors="ignore").strip("\x00")

    print(f"   Version:      v{major}.{minor}.{patch}")
    print(f"   Git Hash:     {git_hash_str}")
    print(f"   Build Time:   {time_str}")
    print(f"   Image Size:   {size} bytes (Actual: {len(data)} bytes)")
    print(f"   Header CRC32: 0x{crc:08X}")

    if size == len(data):
        print("Size Check: PASSED")
    elif size == 0:
        print("Size Check: SKIPPED (size is 0, maybe not patched yet?)")
    else:
        print(f"Size Check: FAILED (header says {size}, actual is {len(data)})")

    if crc == 0:
        print("CRC32 Check: SKIPPED (CRC is 0, maybe not patched yet?)")
    else:
        crc_field_offset = offset + CRC_FIELD_OFFSET
        if crc_field_offset + 4 > len(data):
            print("CRC32 Check: FAILED (CRC field out of range)")
        else:
            data_mutable = bytearray(data)
            data_mutable[crc_field_offset : crc_field_offset + 4] = b"\x00\x00\x00\x00"
            calc_crc = binascii.crc32(data_mutable) & 0xFFFFFFFF

            if calc_crc == crc:
                print("CRC32 Check: PASSED")
            else:
                print(
                    "CRC32 Check: FAILED "
                    f"(header: 0x{crc:08X}, calculated: 0x{calc_crc:08X})"
                )

    sig_requested = signature_path is not None
    sig_path = resolve_signature_path(file_path, signature_path)
    pubkey_path = resolve_pubkey_path(sig_path, pubkey_path)
    verify_signature(file_path, sig_path, pubkey_path, sig_requested)


def main():
    parser = argparse.ArgumentParser(
        description="Verify firmware header, CRC32, and optional OpenSSL signature."
    )
    parser.add_argument("firmware_bin", help="Path to the firmware .bin")
    parser.add_argument(
        "--sig", dest="signature", help="Signature file (.sig or .dev.sig)"
    )
    parser.add_argument("--pubkey", help="Public key PEM for signature verification")
    args = parser.parse_args()

    verify_firmware(args.firmware_bin, args.signature, args.pubkey)


if __name__ == "__main__":
    main()
