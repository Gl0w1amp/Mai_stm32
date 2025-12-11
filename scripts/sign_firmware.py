import sys
import struct
import binascii
import os

def sign_firmware(input_path, output_path):
    if not os.path.exists(input_path):
        print(f"Error: Input file '{input_path}' not found.")
        sys.exit(1)

    print(f"Processing firmware: {input_path}")
    
    with open(input_path, 'rb') as f:
        data = bytearray(f.read())

    # Magic: "AFFI" -> 0x49464641
    magic = b'\x41\x46\x46\x49'
    
    # Find header
    offset = data.find(magic)
    if offset == -1:
        print("Error: Firmware header magic (AFFI) not found!")
        sys.exit(1)
        
    print(f"Found header at offset: 0x{offset:X}")
    
    # --- 1. Update Image Size ---
    file_size = len(data)
    print(f"Firmware size: {file_size} bytes")
    
    # Offset 16 is image_size (uint32)
    # Magic(4) + Major(4) + Minor(4) + Patch(4) = 16
    size_offset = offset + 16
    data[size_offset:size_offset+4] = struct.pack('<I', file_size)
    
    # --- 2. Calculate CRC32 ---
    # Offset 52 is crc32 (uint32)
    # Magic(4) + Ver(12) + Size(4) + Git(16) + Time(16) = 52
    crc_offset = offset + 52
    
    # Clear the CRC field before calculation (it should be 0, but just in case)
    data[crc_offset:crc_offset+4] = b'\x00\x00\x00\x00'
    
    # Calculate CRC32 of the entire binary
    crc = binascii.crc32(data) & 0xFFFFFFFF
    print(f"Calculated CRC32: 0x{crc:08X}")
    
    # Write CRC32 back to header
    data[crc_offset:crc_offset+4] = struct.pack('<I', crc)
    
    # --- 3. Save Signed Firmware ---
    with open(output_path, 'wb') as f:
        f.write(data)
    
    print(f"Success! Signed firmware saved to: {output_path}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python sign_firmware.py <input_bin> <output_bin>")
        sys.exit(1)
        
    sign_firmware(sys.argv[1], sys.argv[2])
