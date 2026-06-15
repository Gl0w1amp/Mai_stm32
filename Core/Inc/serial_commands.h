/*
 * serial_commands.h
 *
 * Command dispatch for parsed CDC serial frames.
 */

#ifndef INC_SERIAL_COMMANDS_H_
#define INC_SERIAL_COMMANDS_H_

#include "serial_protocol.h"
#include "usb_reporter.h"
#include <stdint.h>

void serial_commands_process_frame(const serial_frame_t *frame,
		uint64_t dispatch_cycles);
void serial_commands_process_legacy_ascii(const uint8_t *rx_buffer,
		uint8_t rx_len);

uint64_t benchmark_cycles64(void);
uint32_t benchmark_read_u32_le(const uint8_t *src);
void serial_send_benchmark_reply(uint8_t cmd, const uint8_t *payload,
		uint8_t payload_len, uint64_t dispatch_cycles);
uint8_t serial_send_raw_debug_snapshot(uint8_t sequence, uint8_t part_index);
void heart_beat_refresh(void);
uint8_t controller_role_normalize(uint8_t role);
uint8_t flash_touch_sheet_valid(const uint8_t *sheet);
void serial_send_simple_status(uint8_t command, uint8_t ok);
void usb_cdc_tx_stats_reset(void);
void usb_cdc_tx_stats_snapshot(usb_cdc_tx_stats_t *stats_out,
		uint32_t *high_depth_out, uint32_t *low_depth_out);

#endif /* INC_SERIAL_COMMANDS_H_ */
