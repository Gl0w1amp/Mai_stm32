#ifndef FIRMWARE_HEADER_H
#define FIRMWARE_HEADER_H

#include <stdint.h>

// Magic: "AFFI"
#define FW_HEADER_MAGIC 0x49464641

typedef struct {
    uint32_t magic;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint32_t image_size;
    uint8_t  git_hash[16];
    uint8_t  build_timestamp[16];
    uint32_t crc32;
    uint8_t  reserved[28];
} FirmwareHeader_t;

#endif // FIRMWARE_HEADER_H
