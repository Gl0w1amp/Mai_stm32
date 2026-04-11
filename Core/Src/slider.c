/*
 * slider.c
 *
 *  Created on: Mar 26, 2025
 *      Author: Qinh
 */
#include "slider.h"
#include "main.h"
#include <string.h>

uint8_t slider_scan_flag = 0;

#define SERIAL_COMMAND_QUEUE_LENGTH 8u

static serial_frame_t serial_command_queue[SERIAL_COMMAND_QUEUE_LENGTH];
static volatile uint8_t serial_command_head = 0;
static volatile uint8_t serial_command_tail = 0;
static volatile uint8_t serial_command_count = 0;

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

void serial_command_init(void)
{
	uint32_t primask = serial_lock_irq();

	memset(serial_command_queue, 0, sizeof(serial_command_queue));
	serial_command_head = 0;
	serial_command_tail = 0;
	serial_command_count = 0;

	serial_unlock_irq(primask);
}

uint8_t serial_command_push(const uint8_t *data, uint16_t len)
{
	uint32_t primask;
	serial_frame_t *frame;

	if ((data == NULL) || (len == 0u) || (len > SERIAL_FRAME_MAX_LEN)) {
		return 0;
	}

	primask = serial_lock_irq();
	if (serial_command_count >= SERIAL_COMMAND_QUEUE_LENGTH) {
		serial_unlock_irq(primask);
		return 0;
	}

	frame = &serial_command_queue[serial_command_head];
	frame->len = (uint8_t) len;
	memcpy(frame->data, data, len);
	serial_command_head = (uint8_t) ((serial_command_head + 1u) % SERIAL_COMMAND_QUEUE_LENGTH);
	serial_command_count++;
	serial_unlock_irq(primask);

	return 1;
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



