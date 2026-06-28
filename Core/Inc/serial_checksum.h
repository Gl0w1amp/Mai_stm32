/*
 * serial_checksum.h
 *
 * Header-only inline additive sum-of-bytes checksum shared across modules.
 * Canonical checksum helper for serial frames: returns the 8-bit sum of the
 * given bytes (sum of bytes & 0xFF).
 */
#ifndef INC_SERIAL_CHECKSUM_H_
#define INC_SERIAL_CHECKSUM_H_

#include <stdint.h>
#include <stddef.h>

static inline uint8_t serial_checksum_sum(const uint8_t *buf, uint8_t len)
{
	uint8_t checksum = 0u;

	if (buf == NULL) {
		return 0u;
	}

	for (uint8_t i = 0u; i < len; i++) {
		checksum += buf[i];
	}

	return checksum;
}

#endif /* INC_SERIAL_CHECKSUM_H_ */
