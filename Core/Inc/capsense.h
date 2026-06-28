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

extern packet_capsense_t Touch;
extern uint8_t uart_dma_buffer[128];
extern uint8_t capsense_touch_status[34];
extern volatile uint8_t capsense_data_ready;

void capsense_init();
void capsense_check();
void capsense_input_snapshot_publish(void);
void capsense_on_boot_button(void);

#include "capsense_uart.h"
#include "capsense_calibration.h"
#include "capsense_debug.h"

#endif /* INC_CAPSENSE_H_ */
