/*
 * input_snapshot.c
 *
 * Thread-safe latest input snapshot store.
 */

#include "input_snapshot.h"

#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static input_snapshot_t latest_input_snapshot;
static uint8_t latest_input_snapshot_valid = 0u;
static uint32_t next_input_snapshot_seq = 0u;

static void input_snapshot_pack_touch_bits(uint8_t *dst, const uint8_t *touch_status)
{
	memset(dst, 0, INPUT_SNAPSHOT_TOUCH_BITS_SIZE);
	if (touch_status == NULL) {
		return;
	}

	for (uint8_t i = 0u; i < INPUT_SNAPSHOT_TOUCH_CHANNEL_COUNT; i++) {
		if (touch_status[i] != 0u) {
			dst[i / 8u] |= (uint8_t)(1u << (i % 8u));
		}
	}
}

void input_snapshot_reset(void)
{
	taskENTER_CRITICAL();
	memset(&latest_input_snapshot, 0, sizeof(latest_input_snapshot));
	latest_input_snapshot_valid = 0u;
	next_input_snapshot_seq = 0u;
	taskEXIT_CRITICAL();
}

void input_snapshot_publish_touch(const uint16_t *strength_values,
		const uint8_t *touch_status, const input_link_state_t *link_state,
		uint32_t tick_ms)
{
	if (strength_values == NULL) {
		return;
	}

	taskENTER_CRITICAL();
	memcpy(latest_input_snapshot.touch_strength, strength_values,
			sizeof(latest_input_snapshot.touch_strength));
	input_snapshot_pack_touch_bits(latest_input_snapshot.touch_bits, touch_status);
	if (link_state != NULL) {
		latest_input_snapshot.link = *link_state;
	}
	latest_input_snapshot.seq = next_input_snapshot_seq++;
	latest_input_snapshot.tick_ms = tick_ms;
	latest_input_snapshot_valid = 1u;
	taskEXIT_CRITICAL();
}

void input_snapshot_publish_buttons(const uint8_t *button_bits, uint32_t tick_ms)
{
	if (button_bits == NULL) {
		return;
	}

	taskENTER_CRITICAL();
	memcpy(latest_input_snapshot.button_bits, button_bits,
			INPUT_SNAPSHOT_BUTTON_BITS_SIZE);
	latest_input_snapshot.seq = next_input_snapshot_seq++;
	latest_input_snapshot.tick_ms = tick_ms;
	latest_input_snapshot_valid = 1u;
	taskEXIT_CRITICAL();
}

uint8_t input_snapshot_get_latest(input_snapshot_t *snapshot_out)
{
	uint8_t valid;

	if (snapshot_out == NULL) {
		return 0u;
	}

	taskENTER_CRITICAL();
	valid = latest_input_snapshot_valid;
	if (valid != 0u) {
		*snapshot_out = latest_input_snapshot;
	}
	taskEXIT_CRITICAL();

	return valid;
}
