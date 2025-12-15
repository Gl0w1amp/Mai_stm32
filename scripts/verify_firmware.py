import sys
import struct
import binascii
import os

def verify_firmware(file_path):
    if not os.path.exists(file_path):
        print(f"Error: File '{file_path}' not found.")
        sys.exit(1)

    print(f"Verifying firmware: {file_path}")
    with open(file_path, 'rb') as f:
        data = f.read()

    # Magic: "AFFI" -> 0x49464641 (Little Endian: 41 46 46 49)
    magic = b'\x41\x46\x46\x49'
    offset = data.find(magic)
    
    if offset == -1:
        print("❌ Verification Failed: Magic 'AFFI' not found!")
        return

    print(f"✅ Found Header at offset: 0x{offset:X}")
    
    # Parse Header
    # Struct format: Magic(4s) + Major(I) + Minor(I) + Patch(I) + Size(I) + GitHash(16s) + Time(16s) + CRC(I)
    # Total size: 4 + 4 + 4 + 4 + 4 + 16 + 16 + 4 = 56 bytes
    # Note: The struct in C has 'reserved' array at the end, CRC is at the beginning of reserved.
    # Let's match the C struct layout:
    # uint32_t magic;
    # uint32_t version_major;
    # uint32_t version_minor;
    # uint32_t version_patch;
    # uint32_t image_size;
    # uint8_t  git_hash[16];
    # uint8_t  build_timestamp[16];
    # uint32_t crc32;
    
    header_fmt = '<4sIIII16s16sI'
    header_size = struct.calcsize(header_fmt)
    
    try:
        # Read up to CRC32, ignore the rest of reserved bytes for now
        header_data = data[offset:offset+header_size]
        magic_val, major, minor, patch, size, git_hash, time, crc = struct.unpack(header_fmt, header_data)
        
        # Decode strings (remove null terminators)
        git_hash_str = git_hash.decode('utf-8', errors='ignore').strip('\x00')
        time_str = time.decode('utf-8', errors='ignore').strip('\x00')
        
        print(f"   Version:      v{major}.{minor}.{patch}")
        print(f"   Git Hash:     {git_hash_str}")
        print(f"   Build Time:   {time_str}")
        print(f"   Image Size:   {size} bytes (Actual: {len(data)} bytes)")
        print(f"   Header CRC32: 0x{crc:08X}")

        # Verify Size
        if size == len(data):
            print("✅ Size Check: PASSED")
        elif size == 0:
             print("⚠️  Size Check: SKIPPED (Size is 0, maybe not signed yet?)")
        else:
            print(f"❌ Size Check: FAILED (Header says {size}, actual is {len(data)})")

        # Verify CRC
        if crc == 0:
             print("⚠️  CRC Check:  SKIPPED (CRC is 0, maybe not signed yet?)")
        else:
            # Calculate CRC of the file, assuming the CRC field in header is 0
            data_mutable = bytearray(data)
            # Offset of CRC32 field relative to header start
            # Magic(4) + Ver(12) + Size(4) + Git(16) + Time(16) = 52
            crc_field_offset = offset + 52
            
            # Temporarily zero out the CRC field for calculation
            data_mutable[crc_field_offset:crc_field_offset+4] = b'\x00\x00\x00\x00'
            
            calc_crc = binascii.crc32(data_mutable) & 0xFFFFFFFF
            
            if calc_crc == crc:
                print("✅ CRC32 Check: PASSED")
            else:
                print(f"❌ CRC32 Check: FAILED (Header: 0x{crc:08X}, Calculated: 0x{calc_crc:08X})")

    except Exception as e:
        print(f"❌ Parse Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python verify_firmware.py <firmware_bin>")
        sys.exit(1)
        
    verify_firmware(sys.argv[1])
