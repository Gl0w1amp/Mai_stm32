/*
 * capsense_uart.h
 *
 * Capsense UART/link sub-header. Split out of capsense.h; re-included by
 * capsense.h so existing consumers compile with zero call-site churn.
 */

#ifndef INC_CAPSENSE_UART_H_
#define INC_CAPSENSE_UART_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct{
	uint32_t checksum_accept_count;
	uint32_t rolling_checksum_accept_count;
	uint32_t legacy_accept_count;
	uint32_t short_packet_count;
	uint32_t empty_packet_count;
	uint32_t parse_fail_count;
	uint32_t uart_error_count;
	uint32_t auto_reset_count;
	uint8_t protocol_version;
	uint8_t legacy_payload_offset;
	uint8_t rx_failure_streak;
} capsense_uart_stats_t;

bool capsense_data_proc(uint8_t *uart_dma_buffer);
bool capsense_data_proc_legacy(uint8_t *uart_dma_buffer);
uint8_t capsense_take_latest_snapshot(void);
void capsense_uart_stream_reset(void);
void capsense_uart_stream_feed(const uint8_t *data, uint16_t len,
		uint16_t *accepted_frames_out, uint16_t *rejected_frames_out);
void capsense_uart_stats_get(capsense_uart_stats_t *stats_out);
void capsense_uart_stats_reset(void);
void capsense_uart_stats_note_empty_packet(void);
void capsense_uart_stats_note_parse_fail(void);
void capsense_uart_stats_note_uart_error(void);
void capsense_uart_stats_note_auto_reset(void);
void capsense_uart_stats_set_failure_streak(uint8_t streak);
void capsense_link_state_get(uint32_t *last_good_tick_out, uint32_t *last_error_tick_out, uint8_t *protocol_version_out);
void capsense_request_link_reset(void);
void capsense_uart_on_rx_result(uint16_t accepted_frames, uint16_t rejected_frames);
void capsense_uart_on_error(void);
void capsense_service_pending_reset(void);

#endif /* INC_CAPSENSE_UART_H_ */
