/*
 * serial_protocol.c
 *
 * CDC serial frame parser, checksum validation, and command queue.
 */

#include "serial_protocol.h"

#include "capsense.h"
#include "main.h"
#include "serial_checksum.h"
#include "critical_section.h"
#include <string.h>

#define SERIAL_COMMAND_QUEUE_LENGTH 8u
#define SERIAL_COMMAND_STREAM_BUFFER_SIZE 96u
#define SERIAL_RX_RAW_BUFFER_SIZE 512u
#define SERIAL_RX_DRAIN_CHUNK_SIZE 64u

typedef struct {
	uint8_t byte;
	uint8_t transport;
} serial_rx_raw_byte_t;

static serial_frame_t serial_command_queue[SERIAL_COMMAND_QUEUE_LENGTH];
static volatile uint8_t serial_command_head = 0u;
static volatile uint8_t serial_command_tail = 0u;
static volatile uint8_t serial_command_count = 0u;
static uint8_t serial_command_stream[SERIAL_COMMAND_TRANSPORT_COUNT]
		[SERIAL_COMMAND_STREAM_BUFFER_SIZE];
static uint16_t serial_command_stream_len[SERIAL_COMMAND_TRANSPORT_COUNT] = {0u};
static serial_rx_raw_byte_t serial_rx_raw_buffer[SERIAL_RX_RAW_BUFFER_SIZE];
static volatile uint16_t serial_rx_raw_head = 0u;
static volatile uint16_t serial_rx_raw_tail = 0u;
static volatile uint16_t serial_rx_raw_count = 0u;
static volatile serial_command_transport_t serial_response_transport =
		SERIAL_COMMAND_TRANSPORT_CDC;

static serial_command_transport_t serial_transport_normalize(
		serial_command_transport_t transport)
{
	return (transport < SERIAL_COMMAND_TRANSPORT_COUNT) ?
			transport : SERIAL_COMMAND_TRANSPORT_CDC;
}

static uint8_t serial_frame_is_calibration_cancel_capture(const uint8_t *data,
		uint16_t len)
{
	uint8_t checksum = 0u;

	if ((data == NULL) || (len != 4u) || (data[0] != 0xFFu) ||
			(data[1] != SERIAL_CMD_CALIBRATION_CANCEL_CAPTURE) ||
			(data[2] != 0u)) {
		return 0u;
	}

	checksum = serial_checksum_sum(data, (uint8_t)(len - 1u));

	return (uint8_t)(checksum == data[len - 1u]);
}

static uint8_t serial_frame_is_led_command(const uint8_t *data, uint16_t len)
{
	if ((data == NULL) || (len < 4u) || (data[0] != 0xFFu)) {
		return 0u;
	}

	switch (data[1]) {
	case SERIAL_CMD_LED:
	case SERIAL_CMD_LED_BUTTON:
	case SERIAL_CMD_LED_BILLBOARD:
	case SERIAL_CMD_LED_PWM:
		return 1u;
	default:
		return 0u;
	}
}

static uint8_t serial_frame_checksum_valid(const uint8_t *data, uint16_t len)
{
	uint8_t checksum = 0u;

	if ((data == NULL) || (len < 4u)) {
		return 0u;
	}

	checksum = serial_checksum_sum(data, (uint8_t)(len - 1u));

	return (uint8_t)(checksum == data[len - 1u]);
}

static uint8_t serial_command_queue_replace_led_locked(const uint8_t *data,
		uint16_t len, serial_command_transport_t transport)
{
	uint8_t cmd;

	if (serial_frame_is_led_command(data, len) == 0u) {
		return 0u;
	}

	cmd = data[1];
	for (uint8_t i = 0u; i < serial_command_count; i++) {
		uint8_t idx = (uint8_t)((serial_command_tail + i) %
				SERIAL_COMMAND_QUEUE_LENGTH);
		serial_frame_t *queued = &serial_command_queue[idx];
		if ((queued->len >= 4u) && (queued->data[0] == 0xFFu) &&
				(queued->data[1] == cmd)) {
			queued->len = (uint8_t)len;
			queued->transport = (uint8_t)serial_transport_normalize(transport);
			memcpy(queued->data, data, len);
			return 1u;
		}
	}

	return 0u;
}

static uint8_t serial_command_queue_drop_oldest_led_locked(void)
{
	for (uint8_t i = 0u; i < serial_command_count; i++) {
		uint8_t idx = (uint8_t)((serial_command_tail + i) %
				SERIAL_COMMAND_QUEUE_LENGTH);
		if (serial_frame_is_led_command(serial_command_queue[idx].data,
				serial_command_queue[idx].len) != 0u) {
			for (uint8_t j = i; (uint8_t)(j + 1u) < serial_command_count; j++) {
				uint8_t dst = (uint8_t)((serial_command_tail + j) %
						SERIAL_COMMAND_QUEUE_LENGTH);
				uint8_t src = (uint8_t)((serial_command_tail + j + 1u) %
						SERIAL_COMMAND_QUEUE_LENGTH);
				serial_command_queue[dst] = serial_command_queue[src];
			}
			serial_command_count--;
			serial_command_head = (uint8_t)((serial_command_tail +
					serial_command_count) % SERIAL_COMMAND_QUEUE_LENGTH);
			return 1u;
		}
	}

	return 0u;
}

uint8_t serial_protocol_frame_valid(const uint8_t *frame, uint8_t len)
{
	uint8_t expected_len;

	if ((frame == NULL) || (len < 4u) || (frame[0] != 0xFFu)) {
		return 0u;
	}

	expected_len = (uint8_t)(frame[2] + 4u);
	if (expected_len != len) {
		return 0u;
	}

	return serial_frame_checksum_valid(frame, len);
}

static uint8_t serial_command_queue_push(const uint8_t *data, uint16_t len,
		serial_command_transport_t transport)
{
	uint8_t is_cancel_capture;
	uint32_t primask;
	serial_frame_t *frame;

	if ((data == NULL) || (len == 0u) || (len > SERIAL_FRAME_MAX_LEN)) {
		return 0u;
	}

	transport = serial_transport_normalize(transport);
	is_cancel_capture = serial_frame_is_calibration_cancel_capture(data, len);
	if (is_cancel_capture != 0u) {
		capsense_calibration_request_cancel();
	}

	primask = critical_section_enter();
	if (serial_command_queue_replace_led_locked(data, len, transport) != 0u) {
		critical_section_exit(primask);
		return 1u;
	}

	if (serial_command_count >= SERIAL_COMMAND_QUEUE_LENGTH) {
		if ((serial_frame_is_led_command(data, len) == 0u) &&
				(serial_command_queue_drop_oldest_led_locked() != 0u)) {
			/* keep room for control/status commands under LED floods */
		} else {
			critical_section_exit(primask);
			return is_cancel_capture;
		}
	}

	frame = &serial_command_queue[serial_command_head];
	frame->len = (uint8_t)len;
	frame->transport = (uint8_t)transport;
	memcpy(frame->data, data, len);
	serial_command_head = (uint8_t)((serial_command_head + 1u) %
			SERIAL_COMMAND_QUEUE_LENGTH);
	serial_command_count++;
	critical_section_exit(primask);

	return 1u;
}

void serial_command_init(void)
{
	uint32_t primask = critical_section_enter();

	memset(serial_command_queue, 0, sizeof(serial_command_queue));
	serial_command_head = 0u;
	serial_command_tail = 0u;
	serial_command_count = 0u;
	memset(serial_command_stream, 0, sizeof(serial_command_stream));
	memset(serial_command_stream_len, 0, sizeof(serial_command_stream_len));
	memset(serial_rx_raw_buffer, 0, sizeof(serial_rx_raw_buffer));
	serial_rx_raw_head = 0u;
	serial_rx_raw_tail = 0u;
	serial_rx_raw_count = 0u;
	serial_response_transport = SERIAL_COMMAND_TRANSPORT_CDC;

	critical_section_exit(primask);
}

uint8_t serial_command_feed_transport(const uint8_t *data, uint16_t len,
		serial_command_transport_t transport)
{
	uint8_t pushed_any = 0u;
	uint8_t *stream;
	uint16_t *stream_len;

	if ((data == NULL) || (len == 0u)) {
		return 0u;
	}

	transport = serial_transport_normalize(transport);
	stream = serial_command_stream[transport];
	stream_len = &serial_command_stream_len[transport];

	for (uint16_t i = 0u; i < len; i++) {
		uint8_t byte = data[i];

		if (*stream_len < SERIAL_COMMAND_STREAM_BUFFER_SIZE) {
			stream[(*stream_len)++] = byte;
		} else if (byte == 0xFFu) {
			stream[0] = 0xFFu;
			*stream_len = 1u;
		} else {
			*stream_len = 0u;
		}

		for (;;) {
			uint16_t frame_len;
			uint16_t start = 0u;

			if ((*stream_len > 0u) &&
					(stream[0] == 0x7Bu)) {
				if (*stream_len < 6u) {
					break;
				}
				if (serial_command_queue_push(stream, 6u, transport) != 0u) {
					pushed_any = 1u;
				}
				memmove(stream, stream + 6u, *stream_len - 6u);
				*stream_len = (uint16_t)(*stream_len - 6u);
				continue;
			}

			while ((start < *stream_len) &&
					(stream[start] != 0xFFu)) {
				start++;
			}

			if (start >= *stream_len) {
				*stream_len = 0u;
				break;
			}

			if (start > 0u) {
				memmove(stream, stream + start, *stream_len - start);
				*stream_len = (uint16_t)(*stream_len - start);
			}

			if (*stream_len < 4u) {
				break;
			}

			frame_len = (uint16_t)stream[2] + 4u;
			if ((frame_len < 4u) || (frame_len > SERIAL_FRAME_MAX_LEN)) {
				memmove(stream, stream + 1u, *stream_len - 1u);
				(*stream_len)--;
				continue;
			}

			if (*stream_len < frame_len) {
				break;
			}

			if (serial_frame_checksum_valid(stream, frame_len)) {
				if (serial_command_queue_push(stream, frame_len,
						transport) != 0u) {
					pushed_any = 1u;
				}
				memmove(stream, stream + frame_len,
						*stream_len - frame_len);
				*stream_len = (uint16_t)(*stream_len - frame_len);
				continue;
			}

			memmove(stream, stream + 1u, *stream_len - 1u);
			(*stream_len)--;
		}
	}

	return pushed_any;
}

uint8_t serial_command_feed_isr_transport(const uint8_t *data, uint16_t len,
		serial_command_transport_t transport)
{
	uint32_t primask;

	if ((data == NULL) || (len == 0u)) {
		return 0u;
	}

	transport = serial_transport_normalize(transport);
	primask = critical_section_enter();
	for (uint16_t i = 0u; i < len; i++) {
		if (serial_rx_raw_count >= SERIAL_RX_RAW_BUFFER_SIZE) {
			serial_rx_raw_tail = (uint16_t)((serial_rx_raw_tail + 1u) %
					SERIAL_RX_RAW_BUFFER_SIZE);
			serial_rx_raw_count--;
		}
		serial_rx_raw_buffer[serial_rx_raw_head].byte = data[i];
		serial_rx_raw_buffer[serial_rx_raw_head].transport = (uint8_t)transport;
		serial_rx_raw_head = (uint16_t)((serial_rx_raw_head + 1u) %
				SERIAL_RX_RAW_BUFFER_SIZE);
		serial_rx_raw_count++;
	}
	critical_section_exit(primask);

	return 1u;
}

uint8_t serial_command_drain_rx_stream(void)
{
	uint8_t pushed_any = 0u;
	serial_rx_raw_byte_t chunk[SERIAL_RX_DRAIN_CHUNK_SIZE];

	for (;;) {
		uint16_t chunk_len;
		uint32_t primask = critical_section_enter();

		chunk_len = serial_rx_raw_count;
		if (chunk_len > SERIAL_RX_DRAIN_CHUNK_SIZE) {
			chunk_len = SERIAL_RX_DRAIN_CHUNK_SIZE;
		}
		for (uint16_t i = 0u; i < chunk_len; i++) {
			chunk[i] = serial_rx_raw_buffer[serial_rx_raw_tail];
			serial_rx_raw_tail = (uint16_t)((serial_rx_raw_tail + 1u) %
					SERIAL_RX_RAW_BUFFER_SIZE);
		}
		serial_rx_raw_count = (uint16_t)(serial_rx_raw_count - chunk_len);
		critical_section_exit(primask);

		if (chunk_len == 0u) {
			break;
		}
		for (uint16_t i = 0u; i < chunk_len; i++) {
			if (serial_command_feed_transport(&chunk[i].byte, 1u,
					(serial_command_transport_t)chunk[i].transport) != 0u) {
				pushed_any = 1u;
			}
		}
	}

	return pushed_any;
}

uint8_t serial_command_pop(serial_frame_t *frame)
{
	uint32_t primask;

	if (frame == NULL) {
		return 0u;
	}

	primask = critical_section_enter();
	if (serial_command_count == 0u) {
		critical_section_exit(primask);
		return 0u;
	}

	*frame = serial_command_queue[serial_command_tail];
	serial_command_tail = (uint8_t)((serial_command_tail + 1u) %
			SERIAL_COMMAND_QUEUE_LENGTH);
	serial_command_count--;
	critical_section_exit(primask);

	return 1u;
}

uint8_t serial_command_stream_pending(void)
{
	for (uint8_t i = 0u; i < SERIAL_COMMAND_TRANSPORT_COUNT; i++) {
		if (serial_command_stream_len[i] != 0u) {
			return 1u;
		}
	}
	return (uint8_t)(serial_rx_raw_count != 0u);
}

void serial_command_set_response_transport(serial_command_transport_t transport)
{
	serial_response_transport = serial_transport_normalize(transport);
}
