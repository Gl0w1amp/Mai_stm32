/*
 * slider.h
 *
 *  Created on: Mar 26, 2025
 *      Author: Qinh
 */

#ifndef INC_SLIDER_H_
#define INC_SLIDER_H_
#include <stdint.h>

#define SERIAL_FRAME_MAX_LEN 64u

typedef struct serial_frame {
	uint8_t len;
	uint8_t data[SERIAL_FRAME_MAX_LEN];
} serial_frame_t;

typedef enum serial_cmd {
	SERIAL_CMD_AUTO_SCAN = 0x01,
	SERIAL_CMD_LED = 0x02,
	SERIAL_CMD_SCAN_START = 0x03,
	SERIAL_CMD_SCAN_STOP = 0x04,
	SERIAL_CMD_READ_MONO_THRESHOLD = 0x05,
	SERIAL_CMD_WRITE_MONO_THRESHOLD = 0x06,
	SERIAL_CMD_READ_TOUCH_SHEET = 0x07,
	SERIAL_CMD_WRITE_TOUCH_SHEET = 0x08,
	SERIAL_CMD_TO_DEBUG_MODE= 0x09,
	SERIAL_CMD_SET_DEBUG_CHANNEL = 0x0A,
	SERIAL_CMD_RESET = 0x10,
	SERIAL_CMD_HEART_BEAT = 0x11,
	SERIAL_CMD_READ_DELAY_SETTING = 0x12,
	SERIAL_CMD_WRITE_DELAY_SETTING = 0x13,
	SERIAL_CMD_LED_BUTTON = 0x14,
	SERIAL_CMD_LED_BILLBOARD = 0x15,
	SERIAL_CMD_LED_PWM = 0x16,
	SERIAL_CMD_AUTO_CALIBRATE_THRESHOLD = 0x17,
	SERIAL_CMD_GET_CAPSENSE_UART_STATS = 0x18,
	SERIAL_CMD_GET_USB_CDC_STATS = 0x19,
	SERIAL_CMD_GET_CONTROLLER_ROLE = 0x1A,
	SERIAL_CMD_SET_CONTROLLER_ROLE = 0x1B,
	SERIAL_CMD_JUMP_TO_DFU = 0x21,
	SERIAL_CMD_BENCHMARK = 0x22,
	SERIAL_CMD_BENCHMARK_EVENT = 0x23,
	SERIAL_CMD_BENCHMARK_HID_EVENT = 0x24,
	SERIAL_CMD_GET_BOARD_INFO = 0xF0
} serial_cmd_t;

void slider_set_led();
void slider_scan_start();
void slider_scan_stop();
void slider_reset();
void slider_get_board_info();
void slider_scan();
void slider_notify_command_ready_from_isr(void);
void serial_command_init(void);
uint8_t serial_command_push(const uint8_t *data, uint16_t len);
uint8_t serial_command_pop(serial_frame_t *frame);
uint8_t serial_cdc_tx_enqueue_high(const uint8_t *buf, uint16_t len);
uint8_t serial_cdc_tx_enqueue_low(const uint8_t *buf, uint16_t len);

extern uint8_t slider_scan_flag;

#endif /* INC_SLIDER_H_ */
