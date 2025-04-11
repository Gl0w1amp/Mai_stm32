/*
 * slider.h
 *
 *  Created on: Mar 26, 2025
 *      Author: Qinh
 */

#ifndef INC_SLIDER_H_
#define INC_SLIDER_H_
#include <stdint.h>

extern uint8_t rxBuffer[64];
extern uint8_t rxLen;

typedef enum serial_cmd {
	SERIAL_CMD_AUTO_SCAN = 0x01,
	SERIAL_CMD_LED = 0x02,
	SERIAL_CMD_SCAN_START = 0x03,
	SERIAL_CMD_SCAN_STOP = 0x04,
	SERIAL_CMD_READ_MONO_THRESHOLD = 0x5,
	SERIAL_CMD_WRITE_MONO_THRESHOLD = 0x6,
	SERIAL_CMD_READ_TOUCH_SHEET = 0x7,
	SERIAL_CMD_WRITE_TOUCH_SHEET = 0x8,
	SERIAL_CMD_RESET = 0x10,
	SERIAL_CMD_HEART_BEAT = 0x11,
	SERIAL_CMD_GET_BOARD_INFO = 0xF0
} serial_cmd_t;

void slider_set_led();
void slider_scan_start();
void slider_scan_stop();
void slider_reset();
void slider_get_board_info();
void slider_scan();

extern uint8_t slider_scan_flag;

#endif /* INC_SLIDER_H_ */
