/*
 * serial_reports.c
 *
 * Shared serial/CDC report encoders.
 */

#include "serial_reports.h"

#include "serial_protocol.h"
#include "capsense.h"
#include "benchmark.h"
#include "usb_reporter.h"
#include "usbd_hid_custom_if.h"
#include "main.h"
#include <string.h>

#define RAW_DEBUG_SNAPSHOT_PARTS 2u
#define RAW_DEBUG_SNAPSHOT_VALUES_PER_PART (34u / RAW_DEBUG_SNAPSHOT_PARTS)

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

void serial_send_simple_status(uint8_t command, uint8_t ok)
{
	uint8_t cmd_tmp[5] = {0xff, command, 1u, ok, 0u};

	for(uint8_t i = 0;i<4u;i++){
		cmd_tmp[4] += cmd_tmp[i];
	}
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
}

uint8_t serial_send_raw_debug_snapshot(uint8_t sequence, uint8_t part_index)
{
	uint8_t cmd_tmp[1u + 1u + 1u + 1u + 1u + 1u +
			(RAW_DEBUG_SNAPSHOT_VALUES_PER_PART * 2u) + 1u] = {0};
	uint8_t idx = 0u;
	uint8_t first_channel = (uint8_t) (part_index * RAW_DEBUG_SNAPSHOT_VALUES_PER_PART);

	if (part_index >= RAW_DEBUG_SNAPSHOT_PARTS) {
		return 0u;
	}

	cmd_tmp[idx++] = 0xFFu;
	cmd_tmp[idx++] = SERIAL_CMD_GET_RAW_DEBUG_SNAPSHOT;
	cmd_tmp[idx++] = (uint8_t) (3u + (RAW_DEBUG_SNAPSHOT_VALUES_PER_PART * 2u));
	cmd_tmp[idx++] = sequence;
	cmd_tmp[idx++] = part_index;
	cmd_tmp[idx++] = RAW_DEBUG_SNAPSHOT_PARTS;

	for (uint8_t channel = first_channel;
			channel < (uint8_t) (first_channel + RAW_DEBUG_SNAPSHOT_VALUES_PER_PART);
			channel++) {
		uint16_t raw = Touch.channel_raw[channel];
		cmd_tmp[idx++] = (uint8_t) (raw & 0xFFu);
		cmd_tmp[idx++] = (uint8_t) ((raw >> 8) & 0xFFu);
	}

	for (uint8_t checksum_index = 0u; checksum_index < idx; checksum_index++) {
		cmd_tmp[idx] += cmd_tmp[checksum_index];
	}

	return serial_cdc_tx_enqueue_high(cmd_tmp, (uint16_t) (idx + 1u));
}

/* Echoes the benchmark payload and attaches device-side cycle timestamps. */
void serial_send_benchmark_reply(uint8_t cmd, const uint8_t *payload, uint8_t payload_len, uint64_t dispatch_cycles)
{
	uint8_t cmd_tmp[1 + 1 + 1 + BENCHMARK_MAX_PAYLOAD + 8 + 8 + 4 + 1];
	uint8_t idx = 0;
	uint64_t tx_cycles = benchmark_cycles64();

	if (payload_len > BENCHMARK_MAX_PAYLOAD) {
		payload_len = BENCHMARK_MAX_PAYLOAD;
	}

	cmd_tmp[idx++] = 0xFF;
	cmd_tmp[idx++] = cmd;
	cmd_tmp[idx++] = payload_len + 20;
	memcpy(&cmd_tmp[idx], payload, payload_len);
	idx += payload_len;
	benchmark_write_u64_le(&cmd_tmp[idx], dispatch_cycles);
	idx += 8;
	benchmark_write_u64_le(&cmd_tmp[idx], tx_cycles);
	idx += 8;
	benchmark_write_u32_le(&cmd_tmp[idx], SystemCoreClock);
	idx += 4;
	cmd_tmp[idx] = 0;
	for (uint8_t i = 0; i < idx; i++) {
		cmd_tmp[idx] += cmd_tmp[i];
	}
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, idx + 1);
}

uint8_t serial_capsense_debug_emit_focus(const float *focus_values, uint8_t count,
		const uint8_t *vofa_tail, uint8_t tail_len)
{
	if ((focus_values == NULL) || (count == 0u)) {
		return 0u;
	}
	(void) serial_cdc_tx_enqueue_low((const uint8_t *) focus_values,
			(uint16_t) (count * sizeof(float)));
	if ((vofa_tail != NULL) && (tail_len != 0u)) {
		(void) serial_cdc_tx_enqueue_low(vofa_tail, tail_len);
	}
	return 1u;
}

uint8_t serial_capsense_debug_emit_raw(const uint16_t *raw_values, uint8_t value_count)
{
	return mai2_hid_raw_debug_stream(raw_values, value_count);
}
