/*
 * serial_reports.c
 *
 * Shared serial/CDC report encoders.
 */

#include "serial_reports.h"

#include <string.h>

uint8_t serial_reports_checksum(const uint8_t *buf, uint8_t len)
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

uint8_t serial_reports_build_live_state_frame(uint8_t command,
		const input_snapshot_t *snapshot, uint8_t *buf, uint8_t *len_out)
{
	uint8_t idx = 0u;

	if ((snapshot == NULL) || (buf == NULL) || (len_out == NULL)) {
		return 0u;
	}

	buf[idx++] = 0xFFu;
	buf[idx++] = command;
	buf[idx++] = 0x0Au;
	buf[idx++] = snapshot->button_bits[0] & 0x0Fu;
	buf[idx++] = snapshot->button_bits[0] & 0xF0u;
	buf[idx++] = snapshot->button_bits[1];

	for (uint8_t group = 0u; group < 7u; group++) {
		uint8_t packed = 0u;
		for (uint8_t bit = 0u; bit < 5u; bit++) {
			uint8_t touch_index = (uint8_t)(group * 5u + bit);
			if (touch_index >= INPUT_SNAPSHOT_TOUCH_CHANNEL_COUNT) {
				break;
			}
			if ((snapshot->touch_bits[touch_index / 8u] &
					(uint8_t)(1u << (touch_index % 8u))) != 0u) {
				packed |= (uint8_t)(1u << bit);
			}
		}
		buf[idx++] = packed;
	}

	buf[idx] = serial_reports_checksum(buf, idx);
	*len_out = (uint8_t)(idx + 1u);
	return 1u;
}

uint8_t serial_reports_build_touch_scan_frame(const input_snapshot_t *snapshot,
		uint8_t *buf, uint8_t *len_out)
{
	uint8_t live_state[14] = {0};
	uint8_t live_state_len = 0u;

	if ((buf == NULL) || (len_out == NULL)) {
		return 0u;
	}

	if (serial_reports_build_live_state_frame(0x01u, snapshot,
			live_state, &live_state_len) == 0u) {
		return 0u;
	}
	if (live_state_len != sizeof(live_state)) {
		return 0u;
	}

	memset(buf, 0, 9u);
	buf[0] = 0x28u;
	memcpy(&buf[1], &live_state[6], 7u);
	buf[8] = 0x29u;
	*len_out = 9u;
	return 1u;
}
