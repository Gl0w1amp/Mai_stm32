/*
 * slider.c
 *
 *  Created on: Mar 26, 2025
 *      Author: Qinh
 */
#include "slider.h"
#include "capsense.h"
#include "main.h"
#include <string.h>

uint8_t slider_scan_flag = 0;

#define SERIAL_COMMAND_QUEUE_LENGTH 8u
#define SERIAL_COMMAND_STREAM_BUFFER_SIZE 96u

static serial_frame_t serial_command_queue[SERIAL_COMMAND_QUEUE_LENGTH];
static volatile uint8_t serial_command_head = 0;
static volatile uint8_t serial_command_tail = 0;
static volatile uint8_t serial_command_count = 0;
static uint8_t serial_command_stream[SERIAL_COMMAND_STREAM_BUFFER_SIZE];
static uint16_t serial_command_stream_len = 0u;

static uint8_t serial_frame_is_calibration_cancel_capture(const uint8_t *data, uint16_t len)
{
	uint8_t checksum = 0u;

	if ((data == NULL) || (len != 4u) || (data[0] != 0xFFu) ||
			(data[1] != SERIAL_CMD_CALIBRATION_CANCEL_CAPTURE) ||
			(data[2] != 0u)) {
		return 0u;
	}

	for (uint16_t i = 0u; i < (len - 1u); i++) {
		checksum += data[i];
	}

	return (uint8_t) (checksum == data[len - 1u]);
}

static uint32_t serial_lock_irq(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	return primask;
}

static void serial_unlock_irq(uint32_t primask)
{
	if (primask == 0u) {
		__enable_irq();
	}
}

static uint8_t serial_frame_checksum_valid(const uint8_t *data, uint16_t len)
{
	uint8_t checksum = 0u;

	if ((data == NULL) || (len < 4u)) {
		return 0u;
	}

	for (uint16_t i = 0u; i < (uint16_t) (len - 1u); i++) {
		checksum += data[i];
	}

	return (uint8_t) (checksum == data[len - 1u]);
}

static uint8_t serial_command_queue_push(const uint8_t *data, uint16_t len)
{
	uint8_t is_cancel_capture;
	uint32_t primask;
	serial_frame_t *frame;

	if ((data == NULL) || (len == 0u) || (len > SERIAL_FRAME_MAX_LEN)) {
		return 0u;
	}

	is_cancel_capture = serial_frame_is_calibration_cancel_capture(data, len);
	if (is_cancel_capture != 0u) {
		capsense_calibration_request_cancel();
	}

	primask = serial_lock_irq();
	if (serial_command_count >= SERIAL_COMMAND_QUEUE_LENGTH) {
		serial_unlock_irq(primask);
		return is_cancel_capture;
	}

	frame = &serial_command_queue[serial_command_head];
	frame->len = (uint8_t) len;
	memcpy(frame->data, data, len);
	serial_command_head = (uint8_t) ((serial_command_head + 1u) % SERIAL_COMMAND_QUEUE_LENGTH);
	serial_command_count++;
	serial_unlock_irq(primask);

	return 1u;
}

void serial_command_init(void)
{
	uint32_t primask = serial_lock_irq();

	memset(serial_command_queue, 0, sizeof(serial_command_queue));
	serial_command_head = 0;
	serial_command_tail = 0;
	serial_command_count = 0;
	memset(serial_command_stream, 0, sizeof(serial_command_stream));
	serial_command_stream_len = 0u;

	serial_unlock_irq(primask);
}

uint8_t serial_command_push(const uint8_t *data, uint16_t len)
{
	return serial_command_queue_push(data, len);
}

uint8_t serial_command_feed(const uint8_t *data, uint16_t len)
{
	uint8_t pushed_any = 0u;

	if ((data == NULL) || (len == 0u)) {
		return 0u;
	}

	for (uint16_t i = 0u; i < len; i++) {
		uint8_t byte = data[i];

		if (serial_command_stream_len < SERIAL_COMMAND_STREAM_BUFFER_SIZE) {
			serial_command_stream[serial_command_stream_len++] = byte;
		} else if (byte == 0xFFu) {
			serial_command_stream[0] = 0xFFu;
			serial_command_stream_len = 1u;
		} else {
			serial_command_stream_len = 0u;
		}

		for (;;) {
			uint16_t frame_len;
			uint16_t start = 0u;

			while ((start < serial_command_stream_len) &&
					(serial_command_stream[start] != 0xFFu)) {
				start++;
			}

			if (start >= serial_command_stream_len) {
				serial_command_stream_len = 0u;
				break;
			}

			if (start > 0u) {
				memmove(serial_command_stream, serial_command_stream + start,
						serial_command_stream_len - start);
				serial_command_stream_len = (uint16_t) (serial_command_stream_len - start);
			}

			if (serial_command_stream_len < 4u) {
				break;
			}

			frame_len = (uint16_t) serial_command_stream[2] + 4u;
			if ((frame_len < 4u) || (frame_len > SERIAL_FRAME_MAX_LEN)) {
				memmove(serial_command_stream, serial_command_stream + 1u,
						serial_command_stream_len - 1u);
				serial_command_stream_len--;
				continue;
			}

			if (serial_command_stream_len < frame_len) {
				break;
			}

			if (serial_frame_checksum_valid(serial_command_stream, frame_len)) {
				if (serial_command_queue_push(serial_command_stream, frame_len) != 0u) {
					pushed_any = 1u;
				}
				memmove(serial_command_stream, serial_command_stream + frame_len,
						serial_command_stream_len - frame_len);
				serial_command_stream_len = (uint16_t) (serial_command_stream_len - frame_len);
				continue;
			}

			memmove(serial_command_stream, serial_command_stream + 1u,
					serial_command_stream_len - 1u);
			serial_command_stream_len--;
		}
	}

	return pushed_any;
}

uint8_t serial_command_pop(serial_frame_t *frame)
{
	uint32_t primask;

	if (frame == NULL) {
		return 0;
	}

	primask = serial_lock_irq();
	if (serial_command_count == 0u) {
		serial_unlock_irq(primask);
		return 0;
	}

	*frame = serial_command_queue[serial_command_tail];
	serial_command_tail = (uint8_t) ((serial_command_tail + 1u) % SERIAL_COMMAND_QUEUE_LENGTH);
	serial_command_count--;
	serial_unlock_irq(primask);

	return 1;
}

uint8_t serial_command_stream_pending(void)
{
	return (uint8_t) (serial_command_stream_len != 0u);
}



