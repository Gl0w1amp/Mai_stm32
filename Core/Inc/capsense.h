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

extern packet_capsense_t Touch;
extern uint8_t uart_dma_buffer[128];
extern uint16_t capsense_threshold[34];
extern uint8_t capsense_touch_status[34];
extern uint8_t touch_sheet[34];

void Touch_UART_IDLE_Handler();
void capsense_init();
void capsense_check();
bool capsense_data_proc(uint8_t *uart_dma_buffer);
bool capsense_data_proc_legacy(uint8_t *uart_dma_buffer);
void Boot_Buttom_IRQHandler(void);
uint8_t capsense_auto_calibrate_thresholds(uint16_t *thresholds_out, uint16_t *min_threshold_out, uint16_t *max_threshold_out);
void capsense_uart_stats_get(capsense_uart_stats_t *stats_out);
void capsense_uart_stats_reset(void);
void capsense_uart_stats_note_short_packet(void);
void capsense_uart_stats_note_empty_packet(void);
void capsense_uart_stats_note_parse_fail(void);
void capsense_uart_stats_note_uart_error(void);
void capsense_uart_stats_note_auto_reset(void);
void capsense_uart_stats_set_failure_streak(uint8_t streak);

#endif /* INC_CAPSENSE_H_ */
