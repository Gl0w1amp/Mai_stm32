/*
 * serial_reports.h
 *
 * CDC/serial report encoders.
 */

#ifndef INC_SERIAL_REPORTS_H_
#define INC_SERIAL_REPORTS_H_

#include "input_snapshot.h"
#include <stdint.h>

uint8_t serial_reports_build_live_state_frame(uint8_t command,
		const input_snapshot_t *snapshot, uint8_t *buf, uint8_t *len_out);
uint8_t serial_reports_build_touch_scan_frame(const input_snapshot_t *snapshot,
		uint8_t *buf, uint8_t *len_out);
void serial_send_simple_status(uint8_t command, uint8_t ok);
uint8_t serial_send_raw_debug_snapshot(uint8_t sequence, uint8_t part_index);
void serial_send_benchmark_reply(uint8_t cmd, const uint8_t *payload, uint8_t payload_len, uint64_t dispatch_cycles);
uint8_t serial_capsense_debug_emit_focus(const float *focus_values, uint8_t count,
		const uint8_t *vofa_tail, uint8_t tail_len);
uint8_t serial_capsense_debug_emit_raw(const uint16_t *raw_values, uint8_t value_count);

#endif /* INC_SERIAL_REPORTS_H_ */
