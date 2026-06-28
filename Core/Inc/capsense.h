/*
 * capsense.h
 *
 *  Created on: Jan 8, 2025
 *      Author: Qinh
 */

#ifndef INC_CAPSENSE_H_
#define INC_CAPSENSE_H_

#include <stdint.h>
#include <stdbool.h>

//#define PSOC_DEBUG

typedef union{
	uint8_t data[68];
	struct{
		uint16_t channel_raw[34];
	};
}packet_capsense_t;

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

typedef struct {
	uint32_t service_call_count;
	uint32_t emit_batch_count;
	uint32_t enqueue_attempt_count;
	uint32_t enqueue_success_count;
	uint8_t last_queue_slots;
	uint8_t last_debug_flag;
	uint8_t last_debug_stream_mode;
	uint8_t last_enqueue_success_mask;
} capsense_debug_stats_t;

typedef struct {
	uint8_t logical_index;
	uint8_t best_channel;
	uint8_t confidence;
	uint16_t threshold;
	uint16_t peak_delta;
	uint16_t idle_threshold;
} capsense_calibration_result_t;

#define CAPSENSE_CALIBRATION_CAPTURE_FLAG_RELAXED 0x01u

extern packet_capsense_t Touch;
extern uint8_t uart_dma_buffer[128];
extern uint16_t capsense_threshold[34];
extern uint8_t capsense_touch_status[34];
extern uint8_t touch_sheet[34];
extern volatile uint8_t capsense_data_ready;

void Touch_UART_IDLE_Handler();
void capsense_init();
void capsense_check();
void capsense_debug_service(void);
void capsense_input_snapshot_publish(void);
bool capsense_data_proc(uint8_t *uart_dma_buffer);
bool capsense_data_proc_legacy(uint8_t *uart_dma_buffer);
uint8_t capsense_take_latest_snapshot(void);
void capsense_uart_stream_reset(void);
void capsense_uart_stream_feed(const uint8_t *data, uint16_t len,
		uint16_t *accepted_frames_out, uint16_t *rejected_frames_out);
void Boot_Buttom_IRQHandler(void);
uint8_t capsense_auto_calibrate_thresholds(uint16_t *thresholds_out, uint16_t *min_threshold_out, uint16_t *max_threshold_out);
void capsense_calibration_begin(void);
void capsense_calibration_abort(void);
void capsense_calibration_request_cancel(void);
uint8_t capsense_calibration_capture(uint8_t logical_index, uint8_t capture_flags, capsense_calibration_result_t *result_out);
uint8_t capsense_calibration_commit(void);
void capsense_uart_stats_get(capsense_uart_stats_t *stats_out);
void capsense_uart_stats_reset(void);
void capsense_uart_stats_note_short_packet(void);
void capsense_uart_stats_note_empty_packet(void);
void capsense_uart_stats_note_parse_fail(void);
void capsense_uart_stats_note_uart_error(void);
void capsense_uart_stats_note_auto_reset(void);
void capsense_uart_stats_set_failure_streak(uint8_t streak);
void capsense_debug_stats_get(capsense_debug_stats_t *stats_out);
void capsense_debug_stats_reset(void);
void capsense_link_state_get(uint32_t *last_good_tick_out, uint32_t *last_error_tick_out, uint8_t *protocol_version_out);
void capsense_request_link_reset(void);
void capsense_uart_on_rx_result(uint16_t accepted_frames, uint16_t rejected_frames);
void capsense_uart_on_error(void);
void capsense_service_pending_reset(void);

#endif /* INC_CAPSENSE_H_ */
