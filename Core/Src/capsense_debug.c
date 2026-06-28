/*
 * capsense_debug.c
 *
 * VOFA and raw HID capsense debug reporting.
 */

#include "capsense_internal.h"
#include "serial_protocol.h"
#include "slider.h"
#include "usb_reporter.h"
#include "usbd_cdc_acm_if.h"
#include "usbd_hid_custom_if.h"
#include "string.h"

extern volatile uint8_t debug_flag;
extern volatile uint8_t debug_stream_mode;

static const uint8_t capsense_debug_vofa_tail[CAPSENSE_DEBUG_VOFA_TAIL_SIZE] = {
		0x00u, 0x00u, 0x80u, 0x7Fu
};

static uint8_t capsense_debug_stream_chunk(const float *values, uint8_t count)
{
	if ((values == NULL) || (count == 0u) ||
			(count > CAPSENSE_DEBUG_STREAM_CHUNK_FLOAT_COUNT)) {
		return 0u;
	}

	return serial_cdc_tx_enqueue_low((const uint8_t *) values,
			(uint16_t) (count * sizeof(float)));
}

static uint16_t capsense_debug_enter_line(uint8_t logical_index)
{
	uint8_t channel = capsense_channel_for_logical(logical_index);
	uint8_t hold_index = capsense_hold_index_for_logical(logical_index);
	uint16_t reference = capsense_baseline[channel];

	if ((hold_index != 0xFF) && (capsense_hold_state[hold_index] != CAPSENSE_HOLD_STATE_IDLE)) {
		reference = capsense_freeze[channel];
	}

	return reference + Flash.touch_threshold[logical_index];
}

static uint16_t capsense_debug_release_line(uint8_t logical_index)
{
	uint8_t channel = capsense_channel_for_logical(logical_index);
	uint16_t enter_threshold = Flash.touch_threshold[logical_index];
	uint8_t hold_index = capsense_hold_index_for_logical(logical_index);
	uint16_t release_threshold = capsense_release_threshold(enter_threshold);
	uint16_t reference = capsense_baseline[channel];

	if ((hold_index != 0xFF) && (capsense_hold_state[hold_index] != CAPSENSE_HOLD_STATE_IDLE)) {
		reference = capsense_freeze[channel];
		release_threshold = capsense_hold_release_level[hold_index];
	}

	return reference + release_threshold;
}

void capsense_debug_service(void)
{
	capsense_debug_stats.service_call_count++;
	capsense_debug_stats.last_debug_flag = debug_flag;
	capsense_debug_stats.last_debug_stream_mode = debug_stream_mode;

	if(debug_flag == 0u){
		return;
	}

	if (debug_stream_mode == SERIAL_DEBUG_STREAM_MODE_RAW_34) {
		uint8_t hid_status;

		capsense_debug_stats.enqueue_attempt_count++;
		hid_status = mai2_hid_raw_debug_stream(Touch.channel_raw, 34u);
		if (hid_status == (uint8_t) USBD_OK) {
			capsense_debug_stats.emit_batch_count++;
			capsense_debug_stats.enqueue_success_count++;
			capsense_debug_stats.last_enqueue_success_mask = 0x01u;
		} else {
			capsense_debug_stats.last_enqueue_success_mask = 0x00u;
		}
		capsense_debug_stats.last_queue_slots = hid_status;
		return;
	} else {
		uint8_t logical = debug_channel < 34 ? debug_channel : 0;
		uint8_t channel = capsense_channel_for_logical(logical);
		uint8_t hold_index =
				(logical < 8) ? logical :
				((logical >= 18) && (logical < 26)) ? (logical - 10) : 0xFF;
		float hold_state = -1.0f;
		float hold_duration = 0.0f;
		float focus_values[CAPSENSE_DEBUG_FOCUS_FLOAT_COUNT] = {0};

		if (hold_index != 0xFF) {
			hold_state = (float) capsense_hold_state[hold_index];
			hold_duration = (float) capsense_hold_duration[hold_index];
		}

		focus_values[0] = (float) Touch.channel_raw[channel];
		focus_values[1] = capsense_debug_enter_line(logical);
		focus_values[2] = capsense_debug_release_line(logical);
		focus_values[3] = hold_state;
		focus_values[4] = capsense_touch_status[logical] ? 1.0f : 0.0f;
		focus_values[5] = hold_duration;
		(void) capsense_debug_stream_chunk(focus_values,
				CAPSENSE_DEBUG_FOCUS_FLOAT_COUNT);
		(void) serial_cdc_tx_enqueue_low(capsense_debug_vofa_tail,
				CAPSENSE_DEBUG_VOFA_TAIL_SIZE);
	}
}

void capsense_debug_stats_get(capsense_debug_stats_t *stats_out)
{
	if (stats_out == NULL) {
		return;
	}

	*stats_out = capsense_debug_stats;
}

void capsense_debug_stats_reset(void)
{
	memset(&capsense_debug_stats, 0, sizeof(capsense_debug_stats));
}
