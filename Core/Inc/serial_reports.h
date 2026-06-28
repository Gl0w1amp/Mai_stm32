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
void serial_reply_emit(uint8_t command, const uint8_t *payload, uint8_t payload_len);
void serial_send_simple_status(uint8_t command, uint8_t ok);
uint8_t serial_send_raw_debug_snapshot(uint8_t sequence, uint8_t part_index);
void serial_send_benchmark_reply(uint8_t cmd, const uint8_t *payload, uint8_t payload_len, uint64_t dispatch_cycles);
uint8_t serial_capsense_debug_emit_focus(const float *focus_values, uint8_t count,
		const uint8_t *vofa_tail, uint8_t tail_len);
uint8_t serial_capsense_debug_emit_raw(const uint16_t *raw_values, uint8_t value_count);

#define SERIAL_FRAME_BUILDER_CAP 72u  /* header(3)+payload+checksum(1); largest current frame = usb_cdc_stats 60B */
typedef struct { uint8_t buf[SERIAL_FRAME_BUILDER_CAP]; uint8_t idx; } serial_frame_builder_t;
void sfb_begin(serial_frame_builder_t *b, uint8_t command);
void sfb_put(serial_frame_builder_t *b, const void *src, uint8_t n);
void sfb_put_u8(serial_frame_builder_t *b, uint8_t v);
void sfb_finish_emit(serial_frame_builder_t *b);
uint8_t serial_stats_reset_guard(const uint8_t *rx, void (*reset_fn)(void));

#endif /* INC_SERIAL_REPORTS_H_ */
