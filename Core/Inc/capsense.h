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

#define PSOC_DEBUG

typedef union{
	uint8_t data[68];
	struct{
		uint16_t channel_raw[34];
	};
}packet_capsense_t;

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

#endif /* INC_CAPSENSE_H_ */
