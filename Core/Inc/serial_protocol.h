/*
 * serial_protocol.h
 *
 * CDC serial frame parser and legacy command definitions.
 */

#ifndef INC_SERIAL_PROTOCOL_H_
#define INC_SERIAL_PROTOCOL_H_

#include <stdint.h>

#define SERIAL_FRAME_MAX_LEN 64u
#define SERIAL_DEBUG_STREAM_MODE_FOCUS 0u
#define SERIAL_DEBUG_STREAM_MODE_RAW_34 1u

typedef enum serial_command_transport {
	SERIAL_COMMAND_TRANSPORT_CDC = 0u,
	SERIAL_COMMAND_TRANSPORT_VENDOR_HID = 1u,
	SERIAL_COMMAND_TRANSPORT_COUNT
} serial_command_transport_t;

typedef struct serial_frame {
	uint8_t len;
	uint8_t transport;
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
	SERIAL_CMD_EXIT_DEBUG_MODE = 0x0B,
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
	SERIAL_CMD_CALIBRATION_BEGIN = 0x1C,
	SERIAL_CMD_CALIBRATION_CAPTURE = 0x1D,
	SERIAL_CMD_CALIBRATION_COMMIT = 0x1E,
	SERIAL_CMD_CALIBRATION_ABORT = 0x1F,
	SERIAL_CMD_CALIBRATION_CANCEL_CAPTURE = 0x20,
	SERIAL_CMD_JUMP_TO_DFU = 0x21,
	SERIAL_CMD_BENCHMARK = 0x22,
	SERIAL_CMD_BENCHMARK_EVENT = 0x23,
	SERIAL_CMD_BENCHMARK_HID_EVENT = 0x24,
	SERIAL_CMD_JUMP_TO_BOOTLOADER = 0x25,
	SERIAL_CMD_GET_CAPSENSE_DEBUG_STATS = 0x26,
	SERIAL_CMD_GET_RAW_DEBUG_SNAPSHOT = 0x27,
	SERIAL_CMD_GET_LIVE_STATE = 0x28,
	SERIAL_CMD_GET_TOUCH_HID_STATS = 0x29,
	SERIAL_CMD_LED_MODE = 0x2A,
	SERIAL_CMD_LED_CONFIG = 0x2B,
	SERIAL_CMD_GET_BOARD_INFO = 0xF0
} serial_cmd_t;

void serial_command_init(void);
uint8_t serial_command_feed(const uint8_t *data, uint16_t len);
uint8_t serial_command_feed_isr(const uint8_t *data, uint16_t len);
uint8_t serial_command_feed_transport(const uint8_t *data, uint16_t len,
		serial_command_transport_t transport);
uint8_t serial_command_feed_isr_transport(const uint8_t *data, uint16_t len,
		serial_command_transport_t transport);
uint8_t serial_command_drain_rx_stream(void);
uint8_t serial_command_push(const uint8_t *data, uint16_t len);
uint8_t serial_command_push_transport(const uint8_t *data, uint16_t len,
		serial_command_transport_t transport);
uint8_t serial_command_pop(serial_frame_t *frame);
uint8_t serial_command_stream_pending(void);
uint8_t serial_command_stream_pending_transport(
		serial_command_transport_t transport);
serial_command_transport_t serial_command_response_transport(void);
void serial_command_set_response_transport(
		serial_command_transport_t transport);
uint8_t serial_protocol_frame_valid(const uint8_t *frame, uint8_t len);

#endif /* INC_SERIAL_PROTOCOL_H_ */
