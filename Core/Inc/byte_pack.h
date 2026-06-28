/*
 * byte_pack.h
 *
 * Header-only inline little-endian packers. Write LSB-first exactly as the
 * open-coded (v & 0xFF), (v >> 8) & 0xFF, ... sequences do.
 */
#ifndef INC_BYTE_PACK_H_
#define INC_BYTE_PACK_H_

#include <stdint.h>

static inline void put_u16le(uint8_t *dst, uint16_t v)
{
	dst[0] = (uint8_t)(v & 0xFFu);
	dst[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void put_u32le(uint8_t *dst, uint32_t v)
{
	dst[0] = (uint8_t)(v & 0xFFu);
	dst[1] = (uint8_t)((v >> 8) & 0xFFu);
	dst[2] = (uint8_t)((v >> 16) & 0xFFu);
	dst[3] = (uint8_t)((v >> 24) & 0xFFu);
}

#endif /* INC_BYTE_PACK_H_ */
