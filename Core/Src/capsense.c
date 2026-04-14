/*
 * capsense.c
 *
 *  Created on: Jan 8, 2025
 *      Author: Qinh
 */
#include "capsense.h"
#include "usart.h"
#include "string.h"
#include <math.h>
#include "usbd_cdc_acm_if.h"
#include "cmsis_os.h"
#include "flash.h"
#include "stdbool.h"
#include "slider.h"

#define CAPSENSE_BASELINE_VARIANCE 3000
#define CAPSENSE_BASELINE_VARIANCE_A 1000
#define CAPSENSE_BASELINE_VARIANCE_B 600
#define CAPSENSE_BASELINE_VARIANCE_C 500
#define CAPSENSE_BASELINE_VARIANCE_D 800
#define CAPSENSE_BASELINE_VARIANCE_E 600
#define CAPSENSE_BASELINE_COOLDOWN_FRAMES 8
#define CAPSENSE_BASELINE_RISE_MARGIN 128
#define CAPSENSE_BASELINE_RISE_NUMERATOR 1
#define CAPSENSE_BASELINE_RISE_DENOMINATOR 50
#define CAPSENSE_BASELINE_FALL_NUMERATOR 1
#define CAPSENSE_BASELINE_FALL_DENOMINATOR 5

#define CAPSENSE_TOUCH_ENTER_CONFIRM_SAMPLES 1
#define CAPSENSE_TOUCH_RELEASE_CONFIRM_SAMPLES 2
#define CAPSENSE_LONG_HOLD_RELEASE_CONFIRM_SAMPLES 7
#define CAPSENSE_LONG_HOLD_PROTECT_DURATION 40
#define CAPSENSE_POST_RELEASE_INHIBIT_FRAMES 2
#define CAPSENSE_SHORT_RELEASE_NUMERATOR 1
#define CAPSENSE_SHORT_RELEASE_DENOMINATOR 2
#define CAPSENSE_DYNAMIC_FOLLOW_RISE_NUMERATOR 1
#define CAPSENSE_DYNAMIC_FOLLOW_RISE_DENOMINATOR 2
#define CAPSENSE_DYNAMIC_FOLLOW_FALL_NUMERATOR 1
#define CAPSENSE_DYNAMIC_FOLLOW_FALL_DENOMINATOR 4
#define CAPSENSE_DYNAMIC_FOLLOW_CAP_NUMERATOR 7
#define CAPSENSE_DYNAMIC_FOLLOW_CAP_DENOMINATOR 8
#define CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT 128
#define CAPSENSE_AUTO_THRESHOLD_FRAME_TIMEOUT_MS 20
#define CAPSENSE_AUTO_THRESHOLD_TOTAL_TIMEOUT_MS 5000
#define CAPSENSE_AUTO_THRESHOLD_STABLE_FRAMES 16
#define CAPSENSE_AUTO_THRESHOLD_STEP_MAX 192
#define CAPSENSE_AUTO_THRESHOLD_BASELINE_DELTA_MAX 320
#define CAPSENSE_AUTO_THRESHOLD_TRIM_PERCENT 5
#define CAPSENSE_AUTO_THRESHOLD_TRIM_MULTIPLIER 4
#define CAPSENSE_AUTO_THRESHOLD_MIN 180
#define CAPSENSE_AUTO_THRESHOLD_MAX 4000
#define CAPSENSE_AUTO_THRESHOLD_P2P_CAP_MULTIPLIER 2
#define CAPSENSE_AUTO_THRESHOLD_POS_MULTIPLIER 4
#define CAPSENSE_CALIBRATION_IDLE_SAMPLE_COUNT 32
#define CAPSENSE_CALIBRATION_IDLE_STABLE_FRAMES 12
#define CAPSENSE_CALIBRATION_PRESS_CONFIRM_FRAMES 4
#define CAPSENSE_CALIBRATION_PRESS_HOLD_FRAMES 24
#define CAPSENSE_CALIBRATION_FRAME_TIMEOUT_MS 20
#define CAPSENSE_CALIBRATION_TOTAL_TIMEOUT_MS 6000
#define CAPSENSE_CALIBRATION_PRESS_START_MIN_DELTA 220
#define CAPSENSE_CALIBRATION_RELEASE_DELTA 120
#define CAPSENSE_CALIBRATION_CHANNEL_RATIO_STRICT_NUMERATOR 5
#define CAPSENSE_CALIBRATION_CHANNEL_RATIO_STRICT_DENOMINATOR 4
#define CAPSENSE_CALIBRATION_CHANNEL_RATIO_RELAXED_NUMERATOR 6
#define CAPSENSE_CALIBRATION_CHANNEL_RATIO_RELAXED_DENOMINATOR 5
#define CAPSENSE_UART_FRAME_SIZE 70u
#define CAPSENSE_UART_STREAM_BUFFER_SIZE 512u

typedef enum {
	CAPSENSE_HOLD_STATE_IDLE = 0,
	CAPSENSE_HOLD_STATE_ACTIVE = 1,
	CAPSENSE_HOLD_STATE_RELEASE_CONFIRM = 2
} capsense_hold_state_t;

uint8_t uart_dma_buffer[128];

extern UART_HandleTypeDef huart4;
extern DMA_HandleTypeDef hdma_uart4_rx;
extern FlashData Flash;

packet_capsense_t Touch;
static packet_capsense_t capsense_rx_touch;
uint16_t capsense_raw_windows[10][34];
uint8_t capsense_raw_bet = 0;
uint16_t capsense_hold_duration[16] = {0};
uint16_t capsense_level[8] = {0};
uint16_t capsense_freeze[34];
uint16_t capsense_baseline[34];
uint16_t capsense_threshold[34];
uint16_t capsense_hold_prev_raw[16] = {0};
uint16_t capsense_hold_peak_envelope[16] = {0};
uint16_t capsense_hold_release_level[16] = {0};
uint8_t capsense_hold_baseline_cooldown[16] = {0};
volatile uint8_t capsense_data_ready = 0;
uint8_t capsense_bit;
uint8_t capsense_touch_status[34];
uint8_t capsense_hold_enter_confirm[16] = {0};
uint8_t capsense_hold_release_confirm[16] = {0};
uint8_t capsense_hold_rearm_confirm[16] = {0};
uint8_t capsense_hold_state[16] = {0};
uint8_t capsense_procotl_version = 0;
uint8_t capsense_checksum_last = 0;
uint8_t capsense_legacy_payload_offset = 0;
uint8_t capsense_protocol1_confirm_count = 0;
volatile uint32_t capsense_frame_counter = 0;
static capsense_uart_stats_t capsense_uart_stats = {0};
static volatile uint32_t capsense_last_good_frame_tick = 0;
static volatile uint32_t capsense_last_error_tick = 0;
static volatile uint8_t capsense_reset_pending = 0;
static uint8_t capsense_uart_stream_buffer[CAPSENSE_UART_STREAM_BUFFER_SIZE];
static uint16_t capsense_uart_stream_head = 0;
static uint16_t capsense_uart_stream_tail = 0;

typedef union{
    float raw_data_fl[6];
    uint8_t raw_data_u8[24];
}vofa;

vofa vofa1;
uint8_t debug_channel = 0;
extern uint8_t debug_flag;
static uint16_t capsense_auto_threshold_samples[34][CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT];
static uint16_t capsense_auto_threshold_sorted[CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT];
static uint16_t capsense_calibration_press_samples[34][CAPSENSE_CALIBRATION_PRESS_HOLD_FRAMES];
static uint16_t capsense_calibration_press_sorted[CAPSENSE_CALIBRATION_PRESS_HOLD_FRAMES];
static uint16_t capsense_calibration_threshold_stage[34];
static uint8_t capsense_calibration_mapping_stage[34];
static uint8_t capsense_calibration_active = 0;
static volatile uint8_t capsense_calibration_cancel_requested = 0u;

static void capsense_reset_runtime_state(void);
static void capsense_restart_uart4_rx(void);

static uint8_t capsense_channel_for_logical(uint8_t logical_index)
{
	return Flash.touch_sheet[logical_index];
}

static uint8_t capsense_calibration_ratio_pass(uint32_t primary, uint32_t secondary,
		uint8_t capture_flags)
{
	uint32_t numerator = CAPSENSE_CALIBRATION_CHANNEL_RATIO_STRICT_NUMERATOR;
	uint32_t denominator = CAPSENSE_CALIBRATION_CHANNEL_RATIO_STRICT_DENOMINATOR;

	if (secondary == 0u) {
		return 1u;
	}

	if ((capture_flags & CAPSENSE_CALIBRATION_CAPTURE_FLAG_RELAXED) != 0u) {
		numerator = CAPSENSE_CALIBRATION_CHANNEL_RATIO_RELAXED_NUMERATOR;
		denominator = CAPSENSE_CALIBRATION_CHANNEL_RATIO_RELAXED_DENOMINATOR;
	}

	return (uint8_t) ((primary * denominator) >= (secondary * numerator));
}

static uint8_t capsense_wait_for_next_frame(uint32_t *last_frame_counter, uint32_t start_tick,
		uint32_t total_timeout_ms)
{
	uint32_t timeout_ms = CAPSENSE_CALIBRATION_FRAME_TIMEOUT_MS;

	while (capsense_frame_counter == *last_frame_counter) {
		if (capsense_calibration_cancel_requested != 0u) {
			capsense_calibration_cancel_requested = 0u;
			return 0u;
		}
		if ((timeout_ms == 0u) ||
				((HAL_GetTick() - start_tick) >= total_timeout_ms)) {
			return 0u;
		}
		osDelay(1);
		timeout_ms--;
	}

	*last_frame_counter = capsense_frame_counter;
	return 1u;
}

static uint8_t capsense_any_touch_active(void)
{
	for (uint8_t logical = 0; logical < 34u; logical++) {
		if (capsense_touch_status[logical] != 0u) {
			return 1u;
		}
	}

	return 0u;
}

static uint16_t capsense_clamp_threshold(uint32_t threshold)
{
	if (threshold < CAPSENSE_AUTO_THRESHOLD_MIN) {
		threshold = CAPSENSE_AUTO_THRESHOLD_MIN;
	}
	if (threshold > CAPSENSE_AUTO_THRESHOLD_MAX) {
		threshold = CAPSENSE_AUTO_THRESHOLD_MAX;
	}

	return (uint16_t) threshold;
}

static uint16_t capsense_uart_stream_count(void)
{
	if (capsense_uart_stream_head >= capsense_uart_stream_tail) {
		return (uint16_t) (capsense_uart_stream_head - capsense_uart_stream_tail);
	}

	return (uint16_t) (CAPSENSE_UART_STREAM_BUFFER_SIZE -
			(capsense_uart_stream_tail - capsense_uart_stream_head));
}

static uint16_t capsense_uart_stream_free(void)
{
	return (uint16_t) ((CAPSENSE_UART_STREAM_BUFFER_SIZE - 1u) -
			capsense_uart_stream_count());
}

static uint8_t capsense_uart_stream_peek(uint16_t offset)
{
	uint16_t index = (uint16_t) (capsense_uart_stream_tail + offset);

	if (index >= CAPSENSE_UART_STREAM_BUFFER_SIZE) {
		index = (uint16_t) (index - CAPSENSE_UART_STREAM_BUFFER_SIZE);
	}

	return capsense_uart_stream_buffer[index];
}

static void capsense_uart_stream_drop(uint16_t count)
{
	capsense_uart_stream_tail = (uint16_t) (capsense_uart_stream_tail + count);
	if (capsense_uart_stream_tail >= CAPSENSE_UART_STREAM_BUFFER_SIZE) {
		capsense_uart_stream_tail = (uint16_t) (capsense_uart_stream_tail %
				CAPSENSE_UART_STREAM_BUFFER_SIZE);
	}
}

static void capsense_uart_stream_copy(uint8_t *dst, uint16_t count)
{
	for (uint16_t i = 0; i < count; i++) {
		dst[i] = capsense_uart_stream_peek(i);
	}
}

static uint8_t capsense_uart_frame_is_empty(const uint8_t *frame)
{
	for (uint8_t i = 0; i < (CAPSENSE_UART_FRAME_SIZE - 1u); i++) {
		if (frame[i] != 0u) {
			return 0u;
		}
	}

	return 1u;
}

static uint16_t capsense_release_threshold(uint16_t enter_threshold)
{
	uint32_t numerator = CAPSENSE_SHORT_RELEASE_NUMERATOR;
	uint32_t denominator = CAPSENSE_SHORT_RELEASE_DENOMINATOR;
	uint32_t threshold;

	threshold = ((uint32_t) enter_threshold * numerator) / denominator;

	if (threshold == 0) {
		threshold = 1;
	}

	return (uint16_t) threshold;
}

static uint16_t capsense_dynamic_release_threshold(uint16_t enter_threshold, uint16_t follow_level)
{
	uint32_t threshold = capsense_release_threshold(enter_threshold);

	if (follow_level > threshold) {
		threshold = follow_level;
	}
	if (threshold == 0) {
		threshold = 1;
	}

	return (uint16_t) threshold;
}

static uint16_t capsense_follow_target(uint16_t enter_threshold, uint16_t delta)
{
	uint32_t target = capsense_release_threshold(enter_threshold);
	uint32_t capped_delta =
			((uint32_t) delta * CAPSENSE_DYNAMIC_FOLLOW_CAP_NUMERATOR) /
			CAPSENSE_DYNAMIC_FOLLOW_CAP_DENOMINATOR;

	if (capped_delta > target) {
		target = capped_delta;
	}

	return (uint16_t) target;
}

static uint16_t capsense_update_follow_level(uint16_t follow_level, uint16_t target_level)
{
	uint32_t updated = follow_level;

	if (target_level > follow_level) {
		uint32_t step = ((uint32_t) (target_level - follow_level) *
				CAPSENSE_DYNAMIC_FOLLOW_RISE_NUMERATOR +
				CAPSENSE_DYNAMIC_FOLLOW_RISE_DENOMINATOR - 1) /
				CAPSENSE_DYNAMIC_FOLLOW_RISE_DENOMINATOR;
		updated += step;
	} else if (target_level < follow_level) {
		uint32_t step = ((uint32_t) (follow_level - target_level) *
				CAPSENSE_DYNAMIC_FOLLOW_FALL_NUMERATOR +
				CAPSENSE_DYNAMIC_FOLLOW_FALL_DENOMINATOR - 1) /
				CAPSENSE_DYNAMIC_FOLLOW_FALL_DENOMINATOR;
		if (step > updated) {
			updated = 0;
		} else {
			updated -= step;
		}
	} else {
		updated = target_level;
	}

	return (uint16_t) updated;
}

static uint8_t capsense_hold_index_for_logical(uint8_t logical_index)
{
	if (logical_index < 8) {
		return logical_index;
	}
	if ((logical_index >= 18) && (logical_index < 26)) {
		return (uint8_t) (logical_index - 10);
	}
	return 0xFF;
}

static void capsense_update_idle_baseline(uint8_t hold_index, uint8_t channel, uint16_t raw, uint16_t variance_limit);

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

static void capsense_reset_hold_contact(uint8_t logical, uint8_t hold_index, uint8_t channel, uint16_t raw,
		uint16_t variance_limit)
{
	(void) variance_limit;
	capsense_touch_status[logical] = 0;
	capsense_hold_duration[hold_index] = 0;
	capsense_hold_enter_confirm[hold_index] = 0;
	capsense_hold_release_confirm[hold_index] = 0;
	capsense_hold_rearm_confirm[hold_index] = CAPSENSE_POST_RELEASE_INHIBIT_FRAMES;
	capsense_hold_prev_raw[hold_index] = raw;
	capsense_hold_baseline_cooldown[hold_index] = CAPSENSE_BASELINE_COOLDOWN_FRAMES;
	capsense_hold_state[hold_index] = CAPSENSE_HOLD_STATE_IDLE;
	capsense_freeze[channel] = capsense_baseline[channel];
}

static void capsense_start_hold_contact(uint8_t logical, uint8_t hold_index, uint8_t channel, uint16_t raw,
		uint16_t enter_threshold)
{
	uint16_t delta = raw > capsense_baseline[channel] ? (uint16_t) (raw - capsense_baseline[channel]) : 0;
	uint16_t follow_target = capsense_follow_target(enter_threshold, delta);

	capsense_touch_status[logical] = 1;
	capsense_hold_duration[hold_index] = 1;
	capsense_hold_enter_confirm[hold_index] = 0;
	capsense_hold_release_confirm[hold_index] = 0;
	capsense_hold_rearm_confirm[hold_index] = 0;
	capsense_hold_prev_raw[hold_index] = raw;
	capsense_hold_state[hold_index] = CAPSENSE_HOLD_STATE_ACTIVE;
	capsense_freeze[channel] = capsense_baseline[channel];
	capsense_hold_peak_envelope[hold_index] = follow_target;
	capsense_hold_release_level[hold_index] = capsense_dynamic_release_threshold(enter_threshold, follow_target);
	capsense_hold_baseline_cooldown[hold_index] = CAPSENSE_BASELINE_COOLDOWN_FRAMES;
}

static void capsense_update_idle_baseline(uint8_t hold_index, uint8_t channel, uint16_t raw, uint16_t variance_limit)
{
	uint16_t baseline = capsense_baseline[channel];
	uint32_t updated = baseline;

	if (raw <= baseline) {
		updated = ((uint32_t) baseline * (CAPSENSE_BASELINE_FALL_DENOMINATOR - CAPSENSE_BASELINE_FALL_NUMERATOR)) +
				((uint32_t) raw * CAPSENSE_BASELINE_FALL_NUMERATOR);
		updated /= CAPSENSE_BASELINE_FALL_DENOMINATOR;
	} else {
		if (hold_index < 16) {
			if (capsense_hold_baseline_cooldown[hold_index] > 0) {
				capsense_hold_baseline_cooldown[hold_index]--;
				return;
			}
		}
		if ((((uint32_t) baseline + variance_limit) <= raw) ||
				(((uint32_t) baseline + CAPSENSE_BASELINE_RISE_MARGIN) < raw)) {
			return;
		}
		updated = ((uint32_t) baseline * (CAPSENSE_BASELINE_RISE_DENOMINATOR - CAPSENSE_BASELINE_RISE_NUMERATOR)) +
				((uint32_t) raw * CAPSENSE_BASELINE_RISE_NUMERATOR);
		updated /= CAPSENSE_BASELINE_RISE_DENOMINATOR;
	}

	capsense_baseline[channel] = updated > 60000 ? 60000 : (uint16_t) updated;
}

static void capsense_process_hold_block(uint8_t logical_start, uint8_t hold_offset)
{
	for(uint8_t logical = logical_start; logical < logical_start + 8; logical++){
		uint8_t hold_index = hold_offset + (logical - logical_start);
		uint8_t channel = capsense_channel_for_logical(logical);
		uint16_t raw = Touch.channel_raw[channel];
		uint16_t enter_threshold = Flash.touch_threshold[logical];
		uint16_t static_release_threshold = capsense_release_threshold(enter_threshold);
		capsense_hold_state_t state = (capsense_hold_state_t) capsense_hold_state[hold_index];
		uint16_t reference;
		uint16_t delta_freeze;
		uint16_t release_threshold;
		uint8_t release_confirm_required;
		int variance_idle;

		if(capsense_baseline[channel] == 0){
			capsense_baseline[channel] = raw;
		}
		variance_idle = (int) raw - (int) capsense_baseline[channel];
		capsense_hold_release_level[hold_index] = static_release_threshold;

		if(state == CAPSENSE_HOLD_STATE_IDLE){
			capsense_freeze[channel] = capsense_baseline[channel];
			if(capsense_hold_peak_envelope[hold_index] > 0){
				uint16_t follow_target = capsense_follow_target(enter_threshold, 0);
				capsense_hold_peak_envelope[hold_index] =
						capsense_update_follow_level(capsense_hold_peak_envelope[hold_index], follow_target);
			}
			if(capsense_hold_rearm_confirm[hold_index] > 0){
				capsense_hold_rearm_confirm[hold_index]--;
				capsense_hold_enter_confirm[hold_index] = 0;
				capsense_touch_status[logical] = 0;
				capsense_hold_duration[hold_index] = 0;
				capsense_hold_release_confirm[hold_index] = 0;
				capsense_hold_prev_raw[hold_index] = raw;
				capsense_hold_release_level[hold_index] =
						capsense_dynamic_release_threshold(enter_threshold, capsense_hold_peak_envelope[hold_index]);
				capsense_update_idle_baseline(hold_index, channel, raw, CAPSENSE_BASELINE_VARIANCE_A);
				capsense_freeze[channel] = capsense_baseline[channel];
				continue;
			}
			if((variance_idle > (int) enter_threshold) || (raw >= 0xFF00)){
				if(capsense_hold_enter_confirm[hold_index] < 0xFF){
					capsense_hold_enter_confirm[hold_index]++;
				}
				if(capsense_hold_enter_confirm[hold_index] >= CAPSENSE_TOUCH_ENTER_CONFIRM_SAMPLES){
					capsense_start_hold_contact(logical, hold_index, channel, raw, enter_threshold);
				}else{
					capsense_touch_status[logical] = 0;
					capsense_hold_duration[hold_index] = 0;
					capsense_hold_release_confirm[hold_index] = 0;
					capsense_hold_rearm_confirm[hold_index] = 0;
					capsense_hold_prev_raw[hold_index] = raw;
				}
			}else{
				capsense_hold_enter_confirm[hold_index] = 0;
				capsense_touch_status[logical] = 0;
				capsense_hold_duration[hold_index] = 0;
				capsense_hold_release_confirm[hold_index] = 0;
				capsense_hold_rearm_confirm[hold_index] = 0;
				capsense_hold_prev_raw[hold_index] = raw;
				capsense_hold_release_level[hold_index] =
						capsense_dynamic_release_threshold(enter_threshold, capsense_hold_peak_envelope[hold_index]);
				capsense_update_idle_baseline(hold_index, channel, raw, CAPSENSE_BASELINE_VARIANCE_A);
				capsense_freeze[channel] = capsense_baseline[channel];
			}
			continue;
		}

		reference = capsense_freeze[channel];
		delta_freeze = raw > reference ? (uint16_t) (raw - reference) : 0;
		{
			uint16_t follow_target = capsense_follow_target(enter_threshold, delta_freeze);
		capsense_hold_peak_envelope[hold_index] =
				capsense_update_follow_level(capsense_hold_peak_envelope[hold_index], follow_target);
		}
		release_threshold =
				capsense_dynamic_release_threshold(enter_threshold, capsense_hold_peak_envelope[hold_index]);
		capsense_hold_release_level[hold_index] = release_threshold;
		release_confirm_required = CAPSENSE_TOUCH_RELEASE_CONFIRM_SAMPLES;
		if (capsense_hold_duration[hold_index] >= CAPSENSE_LONG_HOLD_PROTECT_DURATION) {
			release_confirm_required = CAPSENSE_LONG_HOLD_RELEASE_CONFIRM_SAMPLES;
		}

		if((delta_freeze < release_threshold) && (raw < 0xFF00)){
			if(capsense_hold_release_confirm[hold_index] < 0xFF){
				capsense_hold_release_confirm[hold_index]++;
			}
			capsense_hold_state[hold_index] = CAPSENSE_HOLD_STATE_RELEASE_CONFIRM;
			capsense_hold_prev_raw[hold_index] = raw;
			if(capsense_hold_release_confirm[hold_index] >= release_confirm_required){
				capsense_reset_hold_contact(logical, hold_index, channel, raw, CAPSENSE_BASELINE_VARIANCE_A);
			}else{
				capsense_touch_status[logical] = 1;
				if(capsense_hold_duration[hold_index] < 10000){
					capsense_hold_duration[hold_index]++;
				}
			}
			continue;
		}

		capsense_hold_enter_confirm[hold_index] = 0;
		capsense_hold_release_confirm[hold_index] = 0;
		capsense_hold_rearm_confirm[hold_index] = 0;
		capsense_touch_status[logical] = 1;
		capsense_hold_state[hold_index] = CAPSENSE_HOLD_STATE_ACTIVE;
		capsense_hold_prev_raw[hold_index] = raw;
		if(capsense_hold_duration[hold_index] < 10000){
			capsense_hold_duration[hold_index]++;
		}
	}
}

static void capsense_process_standard_block(uint8_t logical_start, uint8_t count, uint16_t variance_limit)
{
	for(uint8_t logical = logical_start; logical < logical_start + count; logical++){
		uint8_t channel = capsense_channel_for_logical(logical);
		uint16_t raw = Touch.channel_raw[channel];
		int variance;

		if(capsense_baseline[channel] == 0){
			capsense_baseline[channel] = raw;
		}
		if(capsense_baseline[channel] + variance_limit > raw){
			float baseline = (capsense_baseline[channel] * 0.8f) + (raw * 0.2f);
			capsense_baseline[channel] = baseline;
		}
		variance = raw - capsense_baseline[channel];
		if((variance > Flash.touch_threshold[logical]) || (raw >= 0xFF00)){
			capsense_touch_status[logical] = 1;
		}
		else{
			capsense_touch_status[logical] = 0;
		}
	}
}

bool check_checksum(uint8_t* data){
	uint8_t checksum = 0;
	for(uint8_t i = 0;i<69;i++){
		checksum += data[i];
	}
	if(checksum == data[69]){
		return true;
	}else{
		return false;
	}
}
uint8_t checksum = 0;

static uint8_t capsense_accept_packet(const uint8_t *data, uint8_t lock_protocol, uint8_t rolling_checksum)
{
	memcpy(&capsense_rx_touch.data[0], data + 1, 68);
	if(lock_protocol){
		capsense_procotl_version = 1;
	}
	if (rolling_checksum) {
		capsense_uart_stats.rolling_checksum_accept_count++;
	} else {
		capsense_uart_stats.checksum_accept_count++;
	}
	capsense_last_good_frame_tick = HAL_GetTick();
	capsense_frame_counter++;
	capsense_data_ready = 1;
	return 1;
}

static uint8_t capsense_accept_legacy_packet(const uint8_t *data, uint8_t payload_offset)
{
	memcpy(&capsense_rx_touch.data[0], data + payload_offset, 68);
	if(capsense_procotl_version == 0){
		capsense_procotl_version = 2;
	}
	capsense_uart_stats.legacy_accept_count++;
	capsense_legacy_payload_offset = payload_offset;
	capsense_protocol1_confirm_count = 0;
	capsense_last_good_frame_tick = HAL_GetTick();
	capsense_frame_counter++;
	capsense_data_ready = 1;
	return 1;
}

static uint8_t capsense_score_legacy_payload(const uint8_t *data, uint8_t payload_offset)
{
	uint8_t low_nibble_zero_count = 0;
	uint8_t non_zero_count = 0;

	for (uint8_t i = 0; i < 34; i++) {
		uint16_t raw = (uint16_t) data[payload_offset + (i * 2)] |
				((uint16_t) data[payload_offset + (i * 2) + 1] << 8);

		if ((raw & 0x000Fu) == 0u) {
			low_nibble_zero_count++;
		}
		if (raw != 0u) {
			non_zero_count++;
		}
	}

	if (non_zero_count < 24u) {
		return 0;
	}

	return low_nibble_zero_count;
}

static uint8_t capsense_detect_legacy_payload_offset(const uint8_t *data)
{
	uint8_t score_offset_2 = capsense_score_legacy_payload(data, 2);
	uint8_t score_offset_1 = capsense_score_legacy_payload(data, 1);

	if (score_offset_2 >= score_offset_1) {
		if (score_offset_2 >= 24u) {
			return 2;
		}
	} else {
		if (score_offset_1 >= 24u) {
			return 1;
		}
	}

	/* Current committed PSoC firmware still lands on the legacy 2-byte header
	 * layout because the packet struct is naturally aligned.
	 */
	return 2;
}

void capsense_uart_stream_reset(void)
{
	capsense_uart_stream_head = 0;
	capsense_uart_stream_tail = 0;
}

void capsense_uart_stream_feed(const uint8_t *data, uint16_t len,
		uint16_t *accepted_frames_out, uint16_t *rejected_frames_out)
{
	uint16_t accepted_frames = 0;
	uint16_t rejected_frames = 0;
	uint8_t frame[CAPSENSE_UART_FRAME_SIZE];

	if (accepted_frames_out != NULL) {
		*accepted_frames_out = 0;
	}
	if (rejected_frames_out != NULL) {
		*rejected_frames_out = 0;
	}
	if ((data == NULL) || (len == 0u)) {
		return;
	}

	for (uint16_t i = 0; i < len; i++) {
		if (capsense_uart_stream_free() == 0u) {
			capsense_uart_stream_drop(1u);
			rejected_frames++;
			capsense_uart_stats_note_parse_fail();
		}

		capsense_uart_stream_buffer[capsense_uart_stream_head] = data[i];
		capsense_uart_stream_head = (uint16_t) (capsense_uart_stream_head + 1u);
		if (capsense_uart_stream_head >= CAPSENSE_UART_STREAM_BUFFER_SIZE) {
			capsense_uart_stream_head = 0u;
		}
	}

	while (capsense_uart_stream_count() >= CAPSENSE_UART_FRAME_SIZE) {
		uint8_t packet_ok;

		if (capsense_uart_stream_peek(0u) != 0u) {
			capsense_uart_stream_drop(1u);
			continue;
		}

		capsense_uart_stream_copy(frame, CAPSENSE_UART_FRAME_SIZE);

		if (capsense_uart_frame_is_empty(frame)) {
			capsense_uart_stats_note_empty_packet();
			capsense_uart_stream_drop(CAPSENSE_UART_FRAME_SIZE);
			continue;
		}

		if ((frame[0] == 0u) && (frame[1] == 0u)) {
			packet_ok = (uint8_t) (capsense_data_proc_legacy(frame) ||
					capsense_data_proc(frame));
		} else {
			packet_ok = (uint8_t) (capsense_data_proc(frame) ||
					capsense_data_proc_legacy(frame));
		}

		if (packet_ok != 0u) {
			accepted_frames++;
			capsense_uart_stream_drop(CAPSENSE_UART_FRAME_SIZE);
		} else {
			rejected_frames++;
			capsense_uart_stats_note_parse_fail();
			capsense_uart_stream_drop(1u);
		}
	}

	if (accepted_frames_out != NULL) {
		*accepted_frames_out = accepted_frames;
	}
	if (rejected_frames_out != NULL) {
		*rejected_frames_out = rejected_frames;
	}
}

//static inline void UART_ClearIdle(UART_HandleTypeDef *huart)
//{
//	//dont use on stm32F1/F2/F3/F4,them has usart v1
//	huart->Instance->ICR = USART_ICR_IDLECF;
//}


//void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//{
//    if (huart->Instance == UART4)
//    {
////    	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_15,1);
////		HAL_UART_DMAStop(&huart4);
//		uint8_t len = 70 - __HAL_DMA_GET_COUNTER(huart4.hdmarx);
//		if(len ==70 ){
//			uint8_t ret = 0;
//			for(uint8_t i = 0;i<70;i++){
//				if(uart_dma_buffer[i] == 0){
//					ret++;
//				}else{
//					break;
//				}
//			}
//			if(ret >= 69){
//				uint8_t tes5 = 0x47;
//						CDC_Transmit(0,&tes5,1);
//				goto end;
//			}
//			if(!capsense_data_proc(uart_dma_buffer)){
//				if(!capsense_data_proc_legacy(uart_dma_buffer)){
//
//				}
//			}
//		}else{
//	    	uint8_t tes5[71] = {0x17};
//	    				memcpy(tes5+1,uart_dma_buffer,70);
//	    						CDC_Transmit(0,&tes5,71);
//		}
//	end:
////		UART_ClearIdle(&huart4);
//		HAL_UART_Receive_DMA(&huart4,uart_dma_buffer,70);
//		return;
//    }
//}
//
//void Touch_UART_IDLE_Handler(){
//	UART_ClearIdle(&huart4);
//	HAL_UART_Receive_DMA(&huart4,uart_dma_buffer,70);
//	__HAL_UART_DISABLE_IT(&huart4, UART_IT_IDLE);
//}

bool capsense_data_proc(uint8_t *uart_dma_buffer){
	uint8_t strict_checksum;
	uint8_t rolling_checksum;

	if((capsense_procotl_version != 0) && (capsense_procotl_version != 1)){
		return false;
	}
    if(uart_dma_buffer[0] == 0){
		if (capsense_procotl_version == 0) {
			uint8_t legacy_score_offset_2 = capsense_score_legacy_payload(uart_dma_buffer, 2);
			uint8_t legacy_score_offset_1 = capsense_score_legacy_payload(uart_dma_buffer, 1);

			if ((uart_dma_buffer[1] == 0u) &&
					(legacy_score_offset_2 >= 24u) &&
					(legacy_score_offset_2 >= (uint8_t) (legacy_score_offset_1 + 4u))) {
				capsense_protocol1_confirm_count = 0;
				return false;
			}
		}

		strict_checksum = 0;
		for(uint8_t i = 0;i<69;i++){
			strict_checksum += uart_dma_buffer[i];
		}
		if(strict_checksum == uart_dma_buffer[69]){
			capsense_checksum_last = uart_dma_buffer[69];
			if (capsense_procotl_version == 1) {
				capsense_protocol1_confirm_count = 0;
				return capsense_accept_packet(uart_dma_buffer, 1, 0);
			}
			if (capsense_protocol1_confirm_count < 0xFFu) {
				capsense_protocol1_confirm_count++;
			}
			return capsense_accept_packet(uart_dma_buffer,
					capsense_protocol1_confirm_count >= 2u ? 1u : 0u, 0);
		}

		/* Compatibility path for older PSoC firmware that accumulates checksum
		 * across frames instead of resetting it each packet.
		 */
		rolling_checksum = capsense_checksum_last + strict_checksum;
		if(rolling_checksum == uart_dma_buffer[69]){
			capsense_checksum_last = uart_dma_buffer[69];
			if (capsense_procotl_version == 1) {
				capsense_protocol1_confirm_count = 0;
				return capsense_accept_packet(uart_dma_buffer, 1, 1);
			}
			if (capsense_protocol1_confirm_count < 0xFFu) {
				capsense_protocol1_confirm_count++;
			}
			return capsense_accept_packet(uart_dma_buffer,
					capsense_protocol1_confirm_count >= 2u ? 1u : 0u, 1);
		}
    }
	capsense_protocol1_confirm_count = 0;
    return false;
}

bool capsense_data_proc_legacy(uint8_t *uart_dma_buffer){
	uint8_t payload_offset;

	if((capsense_procotl_version != 0) && (capsense_procotl_version != 2)){
		return false;
	}
    if((uart_dma_buffer[0] == 0 ) && (uart_dma_buffer[1] == 0)){
		payload_offset = capsense_legacy_payload_offset;
		if ((payload_offset != 1u) && (payload_offset != 2u)) {
			payload_offset = capsense_detect_legacy_payload_offset(uart_dma_buffer);
		} else {
			uint8_t locked_score = capsense_score_legacy_payload(uart_dma_buffer, payload_offset);
			if (locked_score < 16u) {
				payload_offset = capsense_detect_legacy_payload_offset(uart_dma_buffer);
			}
		}
		return capsense_accept_legacy_packet(uart_dma_buffer, payload_offset);
    }
    return false;
}

uint8_t capsense_take_latest_snapshot(void)
{
	uint8_t snapshot_ready = 0;
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	if (capsense_data_ready) {
		memcpy(&Touch, &capsense_rx_touch, sizeof(Touch));
		capsense_data_ready = 0;
		snapshot_ready = 1;
	}
	if (primask == 0u) {
		__enable_irq();
	}

	return snapshot_ready;
}

static void capsense_reset_runtime_state(void)
{
	for(uint8_t i = 0;i<34;i++){
		capsense_baseline[i] = 0;
		capsense_freeze[i] = 0;

		Touch.channel_raw[i] = 0;
		capsense_rx_touch.channel_raw[i] = 0;
		capsense_touch_status[i] = 0;
	}
	for(uint8_t i = 0;i<16;i++){
		capsense_hold_duration[i] = 0;
		capsense_hold_enter_confirm[i] = 0;
		capsense_hold_release_confirm[i] = 0;
		capsense_hold_rearm_confirm[i] = 0;
		capsense_hold_baseline_cooldown[i] = 0;
		capsense_hold_state[i] = CAPSENSE_HOLD_STATE_IDLE;
		capsense_hold_prev_raw[i] = 0;
		capsense_hold_peak_envelope[i] = 0;
		capsense_hold_release_level[i] = 0;
	}
	capsense_procotl_version = 0;
	capsense_checksum_last = 0;
	capsense_legacy_payload_offset = 0;
	capsense_protocol1_confirm_count = 0;
	capsense_data_ready = 0;
	capsense_uart_stats.protocol_version = 0;
	capsense_uart_stats.legacy_payload_offset = 0;
	capsense_uart_stats.rx_failure_streak = 0;
	capsense_last_good_frame_tick = 0;
	capsense_last_error_tick = 0;
	capsense_reset_pending = 0;
	capsense_uart_stream_reset();
}

static void capsense_restart_uart4_rx(void)
{
	(void) HAL_UART_DMAStop(&huart4);
	__HAL_UART_CLEAR_IT(&huart4,
			UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF | UART_CLEAR_PEF);
	while(HAL_UARTEx_ReceiveToIdle_DMA(&huart4, uart_dma_buffer, 128) != HAL_OK){

	}
	__HAL_DMA_DISABLE_IT(&hdma_uart4_rx, DMA_IT_HT);
}

void Boot_Buttom_IRQHandler(){
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_3,0);
	capsense_reset_runtime_state();
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_3,1);
}

void capsense_init(){
	capsense_reset_runtime_state();
	capsense_restart_uart4_rx();
	osDelay(100);
	for(uint8_t i = 0;i<34;i++){
		capsense_baseline[i] = Touch.channel_raw[i];
		capsense_freeze[i] = Touch.channel_raw[i];
	}
	for(uint8_t i = 0;i<16;i++){
		capsense_hold_baseline_cooldown[i] = 0;
	}
}

void capsense_calibration_begin(void)
{
	memcpy(capsense_calibration_threshold_stage, Flash.touch_threshold,
			sizeof(capsense_calibration_threshold_stage));
	memcpy(capsense_calibration_mapping_stage, Flash.touch_sheet,
			sizeof(capsense_calibration_mapping_stage));
	capsense_calibration_cancel_requested = 0u;
	capsense_calibration_active = 1u;
}

void capsense_calibration_abort(void)
{
	capsense_calibration_cancel_requested = 0u;
	capsense_calibration_active = 0u;
}

void capsense_calibration_request_cancel(void)
{
	capsense_calibration_cancel_requested = 1u;
}

uint8_t capsense_calibration_capture(uint8_t logical_index, uint8_t capture_flags,
		capsense_calibration_result_t *result_out)
{
	uint32_t idle_sum[34] = {0};
	uint32_t press_sum[34] = {0};
	uint16_t idle_min[34] = {0};
	uint16_t idle_max[34] = {0};
	uint16_t press_peak[34] = {0};
	uint16_t prev_raw[34] = {0};
	uint16_t idle_baseline[34] = {0};
	uint32_t start_tick;
	uint32_t last_frame_counter;
	uint8_t idle_samples = 0u;
	uint8_t stable_frames = 0u;
	uint8_t prev_raw_valid = 0u;
	uint8_t press_confirm = 0u;
	uint8_t press_started = 0u;
	uint8_t press_frames = 0u;

	if ((logical_index >= 34u) || (result_out == NULL) ||
			(capsense_calibration_active == 0u)) {
		return 0u;
	}

	capsense_calibration_cancel_requested = 0u;
	start_tick = HAL_GetTick();
	last_frame_counter = capsense_frame_counter;

	while (idle_samples < CAPSENSE_CALIBRATION_IDLE_SAMPLE_COUNT) {
		uint8_t frame_valid = 1u;

		if (capsense_calibration_cancel_requested != 0u) {
			capsense_calibration_cancel_requested = 0u;
			return 0u;
		}

		if (!capsense_wait_for_next_frame(&last_frame_counter, start_tick,
				CAPSENSE_CALIBRATION_TOTAL_TIMEOUT_MS)) {
			return 0u;
		}

		if (capsense_any_touch_active()) {
			frame_valid = 0u;
		}

		for (uint8_t channel = 0u; channel < 34u; channel++) {
			uint16_t raw = Touch.channel_raw[channel];
			uint16_t step_delta = 0u;

			if (raw >= 0xFF00u) {
				frame_valid = 0u;
				break;
			}

			if (prev_raw_valid != 0u) {
				step_delta = raw > prev_raw[channel] ?
						(uint16_t) (raw - prev_raw[channel]) :
						(uint16_t) (prev_raw[channel] - raw);
				if (step_delta > CAPSENSE_AUTO_THRESHOLD_STEP_MAX) {
					frame_valid = 0u;
					break;
				}
			}

			prev_raw[channel] = raw;
		}

		if (frame_valid == 0u) {
			idle_samples = 0u;
			stable_frames = 0u;
			prev_raw_valid = 0u;
			memset(idle_sum, 0, sizeof(idle_sum));
			memset(idle_min, 0, sizeof(idle_min));
			memset(idle_max, 0, sizeof(idle_max));
			continue;
		}

		prev_raw_valid = 1u;

		if (stable_frames < CAPSENSE_CALIBRATION_IDLE_STABLE_FRAMES) {
			stable_frames++;
			continue;
		}

		for (uint8_t channel = 0u; channel < 34u; channel++) {
			uint16_t raw = Touch.channel_raw[channel];

			idle_sum[channel] += raw;
			if (idle_samples == 0u) {
				idle_min[channel] = raw;
				idle_max[channel] = raw;
			} else {
				if (raw < idle_min[channel]) {
					idle_min[channel] = raw;
				}
				if (raw > idle_max[channel]) {
					idle_max[channel] = raw;
				}
			}
		}

		idle_samples++;
	}

	for (uint8_t channel = 0u; channel < 34u; channel++) {
		idle_baseline[channel] =
				(uint16_t) (idle_sum[channel] / CAPSENSE_CALIBRATION_IDLE_SAMPLE_COUNT);
	}

	start_tick = HAL_GetTick();
	last_frame_counter = capsense_frame_counter;

	while (1) {
		uint16_t frame_delta[34] = {0};
		uint16_t top_delta = 0u;
		uint16_t second_delta = 0u;
		uint8_t top_channel = 0xFFu;
		uint8_t frame_valid = 1u;

		if (capsense_calibration_cancel_requested != 0u) {
			capsense_calibration_cancel_requested = 0u;
			return 0u;
		}

		if (!capsense_wait_for_next_frame(&last_frame_counter, start_tick,
				CAPSENSE_CALIBRATION_TOTAL_TIMEOUT_MS)) {
			return 0u;
		}

		for (uint8_t channel = 0u; channel < 34u; channel++) {
			uint16_t raw = Touch.channel_raw[channel];
			uint16_t delta = raw > idle_baseline[channel] ?
					(uint16_t) (raw - idle_baseline[channel]) : 0u;

			/* Strong presses can legitimately saturate a channel. The normal
			 * runtime touch path already treats raw >= 0xFF00 as active, so
			 * guided calibration must not discard those frames.
			 */

			frame_delta[channel] = delta;
			if (delta >= top_delta) {
				second_delta = top_delta;
				top_delta = delta;
				top_channel = channel;
			} else if (delta > second_delta) {
				second_delta = delta;
			}
		}

		if (frame_valid == 0u) {
			press_confirm = 0u;
			if (press_started != 0u) {
				break;
			}
			continue;
		}

		if (press_started == 0u) {
			uint32_t idle_threshold;

			if (top_channel == 0xFFu) {
				continue;
			}

			idle_threshold = (uint32_t) (idle_max[top_channel] - idle_min[top_channel]) *
					CAPSENSE_AUTO_THRESHOLD_TRIM_MULTIPLIER;
			if (idle_threshold < CAPSENSE_CALIBRATION_PRESS_START_MIN_DELTA) {
				idle_threshold = CAPSENSE_CALIBRATION_PRESS_START_MIN_DELTA;
			}

			if ((top_delta >= idle_threshold) &&
					capsense_calibration_ratio_pass((uint32_t) top_delta,
							(uint32_t) second_delta, capture_flags)) {
				press_confirm++;
				if (press_confirm >= CAPSENSE_CALIBRATION_PRESS_CONFIRM_FRAMES) {
					press_started = 1u;
					press_frames = 0u;
				}
			} else {
				press_confirm = 0u;
			}
			continue;
		}

		for (uint8_t channel = 0u; channel < 34u; channel++) {
			uint16_t delta = frame_delta[channel];

			capsense_calibration_press_samples[channel][press_frames] = delta;
			press_sum[channel] += delta;
			if (delta > press_peak[channel]) {
				press_peak[channel] = delta;
			}
		}

		press_frames++;
		if ((press_frames >= CAPSENSE_CALIBRATION_PRESS_HOLD_FRAMES) ||
				((press_frames >= 8u) && (top_delta <= CAPSENSE_CALIBRATION_RELEASE_DELTA))) {
			break;
		}
	}

	if ((press_started == 0u) || (press_frames < 8u)) {
		return 0u;
	}

	{
		uint32_t best_score = 0u;
		uint32_t second_score = 0u;
		uint8_t best_channel = 0xFFu;

		for (uint8_t channel = 0u; channel < 34u; channel++) {
			uint32_t score = press_sum[channel] / press_frames;

			if (score >= best_score) {
				second_score = best_score;
				best_score = score;
				best_channel = channel;
			} else if (score > second_score) {
				second_score = score;
			}
		}

		if (best_channel == 0xFFu) {
			return 0u;
		}

		{
			uint16_t idle_threshold = capsense_clamp_threshold(
					(uint32_t) (idle_max[best_channel] - idle_min[best_channel]) *
					CAPSENSE_AUTO_THRESHOLD_TRIM_MULTIPLIER);
			uint16_t press_reference;
			uint16_t threshold;
			uint8_t p25_index;
			uint32_t confidence;

			memcpy(capsense_calibration_press_sorted,
					capsense_calibration_press_samples[best_channel],
					(size_t) press_frames * sizeof(uint16_t));
			for (uint8_t i = 1u; i < press_frames; i++) {
				uint16_t value = capsense_calibration_press_sorted[i];
				uint8_t j = i;

				while ((j > 0u) && (capsense_calibration_press_sorted[j - 1u] > value)) {
					capsense_calibration_press_sorted[j] =
							capsense_calibration_press_sorted[j - 1u];
					j--;
				}
				capsense_calibration_press_sorted[j] = value;
			}

			p25_index = (uint8_t) (((uint16_t) (press_frames - 1u) * 25u) / 100u);
			press_reference = capsense_calibration_press_sorted[p25_index];

			if ((press_reference <= idle_threshold) ||
					(capsense_calibration_ratio_pass((uint32_t) press_reference,
							(uint32_t) second_score, capture_flags) == 0u)) {
				return 0u;
			}

			threshold = capsense_clamp_threshold(
					idle_threshold +
					(((uint32_t) (press_reference - idle_threshold)) * 35u) / 100u);
			confidence = second_score == 0u ? 255u :
					((uint32_t) best_score * 255u) / second_score;
			if (confidence > 255u) {
				confidence = 255u;
			}

			if (capsense_calibration_cancel_requested != 0u) {
				capsense_calibration_cancel_requested = 0u;
				return 0u;
			}

			capsense_calibration_mapping_stage[logical_index] = best_channel;
			capsense_calibration_threshold_stage[logical_index] = threshold;

			result_out->logical_index = logical_index;
			result_out->best_channel = best_channel;
			result_out->confidence = (uint8_t) confidence;
			result_out->threshold = threshold;
			result_out->peak_delta = press_peak[best_channel];
			result_out->idle_threshold = idle_threshold;
		}
	}

	return 1u;
}

uint8_t capsense_calibration_commit(void)
{
	if (capsense_calibration_active == 0u) {
		return 0u;
	}

	memcpy(Flash.touch_threshold, capsense_calibration_threshold_stage,
			sizeof(capsense_calibration_threshold_stage));
	memcpy(Flash.touch_sheet, capsense_calibration_mapping_stage,
			sizeof(capsense_calibration_mapping_stage));
	flash_write(Flash.raw_flash);
	capsense_calibration_cancel_requested = 0u;
	capsense_calibration_active = 0u;

	return 1u;
}

//void capsense_baseline_updata(uint8_t channel){
//	float average = 0;
//	for(uint8_t k = 0;k < 10;k++){
//		average += capsense_raw_windows[k][channel]/10;
//	}
//	float variance = 0;
//	for (uint8_t k = 0;k < 10;k++) {
//		variance += powf(capsense_raw_windows[k][channel] - average, 2);
//	}
//	variance /= 10;
//	if(variance < CAPSENSE_BASELINE_VARIANCE){
//		capsense_baseline[channel] = average;
//	}
//}

void capsense_check(){
	capsense_process_hold_block(0, 0);
	capsense_process_standard_block(8, 8, CAPSENSE_BASELINE_VARIANCE_B);
	capsense_process_standard_block(16, 2, CAPSENSE_BASELINE_VARIANCE_C);
	capsense_process_hold_block(18, 8);
	capsense_process_standard_block(26, 8, CAPSENSE_BASELINE_VARIANCE_E);

//	//BLOCK A
//	for(uint8_t i = 0;i<8;i++){
//		if(capsense_duration[i] > 250){
//			capsense_baseline[i] = capsense_orinigal[i];
//		}
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//			if(capsense_duration[i] < 65535){
//				capsense_duration[i] ++;
//			}
//		}else{
//			capsense_touch_status[i] = 0;
//			capsense_duration[i] = 0;
//		}
//		if((Touch.channel_raw[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < 0xFFF0) && capsense_duration[i] <= 250){
//			float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.85) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.15);
//			if(capsense_touch_status[i]){
//				if(baseline + Flash.touch_threshold[i]+ CAPSENSE_BASELINE_VARIANCE < Touch.channel_raw[Flash.touch_sheet[i]]){
//					capsense_baseline[Flash.touch_sheet[i]] = baseline;
//				}
//			}else{
//				capsense_baseline[Flash.touch_sheet[i]] = baseline;
//			}
//		}
//	}
//	//BLOCK B
//	for(uint8_t i = 8;i<16;i++){
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//		}else{
//			capsense_touch_status[i] = 0;
//		}
//	}
//	//BLOCK C
//	for(uint8_t i = 16;i<18;i++){
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//		}else{
//			capsense_touch_status[i] = 0;
//		}
//	}
//	//BLOCK D
//	for(uint8_t i = 18;i<26;i++){
//		if(capsense_duration[i] > 250){
//			capsense_baseline[i] = capsense_orinigal[i];
//		}
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//			if(capsense_duration[i] < 65535){
//				capsense_duration[i] ++;
//			}
//		}else{
//			capsense_touch_status[i] = 0;
//			capsense_duration[i] = 0;
//		}
//		if((Touch.channel_raw[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < 0xFFF0) && capsense_duration[i] <= 250){
//			float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.85) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.15);
//			if(capsense_touch_status[i]){
//				if(baseline + Flash.touch_threshold[i]+ CAPSENSE_BASELINE_VARIANCE < Touch.channel_raw[Flash.touch_sheet[i]]){
//					capsense_baseline[Flash.touch_sheet[i]] = baseline;
//				}
//			}else{
//				capsense_baseline[Flash.touch_sheet[i]] = baseline;
//			}
//		}
//	}
//	//BLOCK E
//	for(uint8_t i = 26;i<34;i++){
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//		}else{
//			capsense_touch_status[i] = 0;
//		}
//	}
//	capsense_data_ready = 0;



	if(debug_flag){
		uint8_t logical = debug_channel < 34 ? debug_channel : 0;
		uint8_t channel = capsense_channel_for_logical(logical);
		uint8_t hold_index =
				(logical < 8) ? logical :
				((logical >= 18) && (logical < 26)) ? (logical - 10) : 0xFF;
		float hold_state = -1.0f;
		float hold_duration = 0.0f;
		static uint8_t tmp[28] = {
				0,0,0,0,0,0,0,0,0,0,
				0,0,0,0,0,0,0,0,0,0,
				0,0,0,0,0,0,0x80,0x7f
		};

		if (hold_index != 0xFF) {
			hold_state = (float) capsense_hold_state[hold_index];
			hold_duration = (float) capsense_hold_duration[hold_index];
		}

		vofa1.raw_data_fl[0] = Touch.channel_raw[channel];
		vofa1.raw_data_fl[1] = capsense_debug_enter_line(logical);
		vofa1.raw_data_fl[2] = capsense_debug_release_line(logical);
		vofa1.raw_data_fl[3] = hold_state;
		vofa1.raw_data_fl[4] = capsense_touch_status[logical] ? 1.0f : 0.0f;
		vofa1.raw_data_fl[5] = hold_duration;
		memcpy(tmp,vofa1.raw_data_u8,24);
		(void) serial_cdc_tx_enqueue_low(tmp, 28);
	}
}

uint8_t capsense_auto_calibrate_thresholds(uint16_t *thresholds_out, uint16_t *min_threshold_out, uint16_t *max_threshold_out)
{
	uint16_t min_raw[34];
	uint16_t max_raw[34];
	uint16_t max_positive_delta[34];
	uint16_t raw_frame[34];
	uint16_t positive_delta_frame[34];
	uint16_t prev_raw[34];
	uint16_t threshold_min = 0xFFFF;
	uint16_t threshold_max = 0;
	uint32_t last_frame_counter;
	uint32_t start_tick;
	uint16_t sample_count = 0;
	uint8_t stable_frames = 0;
	uint8_t prev_raw_valid = 0;
	const uint16_t trim_low_index = (CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT * CAPSENSE_AUTO_THRESHOLD_TRIM_PERCENT) / 100;
	const uint16_t trim_high_index = CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT - 1 - trim_low_index;

	if (thresholds_out == NULL) {
		return 0;
	}
	last_frame_counter = capsense_frame_counter;
	start_tick = HAL_GetTick();

	while (sample_count < CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT) {
		uint32_t timeout_ms = CAPSENSE_AUTO_THRESHOLD_FRAME_TIMEOUT_MS;
		uint8_t frame_valid = 1;

		while (capsense_frame_counter == last_frame_counter) {
			if ((timeout_ms == 0) || ((HAL_GetTick() - start_tick) >= CAPSENSE_AUTO_THRESHOLD_TOTAL_TIMEOUT_MS)) {
				return 0;
			}
			osDelay(1);
			timeout_ms--;
		}
		last_frame_counter = capsense_frame_counter;

		for (uint8_t logical = 0; logical < 34; logical++) {
			uint8_t channel = capsense_channel_for_logical(logical);
			uint16_t raw = Touch.channel_raw[channel];
			uint16_t baseline = capsense_baseline[channel];
			uint16_t positive_delta = raw > baseline ? (uint16_t) (raw - baseline) : 0;
			uint16_t step_delta = 0;

			if (prev_raw_valid) {
				step_delta = raw > prev_raw[logical] ? (uint16_t) (raw - prev_raw[logical]) :
						(uint16_t) (prev_raw[logical] - raw);
			}

			if (capsense_touch_status[logical] || (raw >= 0xFF00) ||
					(positive_delta > CAPSENSE_AUTO_THRESHOLD_BASELINE_DELTA_MAX) ||
					(prev_raw_valid && (step_delta > CAPSENSE_AUTO_THRESHOLD_STEP_MAX))) {
				frame_valid = 0;
				break;
			}

			raw_frame[logical] = raw;
			positive_delta_frame[logical] = positive_delta;
		}

		if (!frame_valid) {
			stable_frames = 0;
			sample_count = 0;
			prev_raw_valid = 0;
			continue;
		}

		memcpy(prev_raw, raw_frame, sizeof(prev_raw));
		prev_raw_valid = 1;

		if (stable_frames < CAPSENSE_AUTO_THRESHOLD_STABLE_FRAMES) {
			stable_frames++;
			continue;
		}

		for (uint8_t logical = 0; logical < 34; logical++) {
			uint16_t raw = raw_frame[logical];
			uint16_t positive_delta = positive_delta_frame[logical];

			capsense_auto_threshold_samples[logical][sample_count] = raw;

			if (sample_count == 0) {
				min_raw[logical] = raw;
				max_raw[logical] = raw;
				max_positive_delta[logical] = positive_delta;
				continue;
			}

			if (raw < min_raw[logical]) {
				min_raw[logical] = raw;
			}
			if (raw > max_raw[logical]) {
				max_raw[logical] = raw;
			}
			if (positive_delta > max_positive_delta[logical]) {
				max_positive_delta[logical] = positive_delta;
			}
		}

		sample_count++;
	}

	for (uint8_t logical = 0; logical < 34; logical++) {
		uint32_t peak_to_peak = (uint32_t) max_raw[logical] - (uint32_t) min_raw[logical];
		uint32_t trimmed_span;
		uint32_t threshold;
		uint32_t positive_candidate = (uint32_t) max_positive_delta[logical] * CAPSENSE_AUTO_THRESHOLD_POS_MULTIPLIER;

		memcpy(capsense_auto_threshold_sorted, capsense_auto_threshold_samples[logical],
				sizeof(capsense_auto_threshold_sorted));

		for (uint16_t i = 1; i < CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT; i++) {
			uint16_t value = capsense_auto_threshold_sorted[i];
			uint16_t j = i;

			while ((j > 0) && (capsense_auto_threshold_sorted[j - 1] > value)) {
				capsense_auto_threshold_sorted[j] = capsense_auto_threshold_sorted[j - 1];
				j--;
			}
			capsense_auto_threshold_sorted[j] = value;
		}

		trimmed_span = (uint32_t) capsense_auto_threshold_sorted[trim_high_index] -
				(uint32_t) capsense_auto_threshold_sorted[trim_low_index];
		threshold = trimmed_span * CAPSENSE_AUTO_THRESHOLD_TRIM_MULTIPLIER;
		if ((peak_to_peak * CAPSENSE_AUTO_THRESHOLD_P2P_CAP_MULTIPLIER) < threshold) {
			threshold = peak_to_peak * CAPSENSE_AUTO_THRESHOLD_P2P_CAP_MULTIPLIER;
		}
		if (positive_candidate > threshold) {
			threshold = positive_candidate;
		}
		if (threshold < CAPSENSE_AUTO_THRESHOLD_MIN) {
			threshold = CAPSENSE_AUTO_THRESHOLD_MIN;
		}
		if (threshold > CAPSENSE_AUTO_THRESHOLD_MAX) {
			threshold = CAPSENSE_AUTO_THRESHOLD_MAX;
		}

		Flash.touch_threshold[logical] = (uint16_t) threshold;
		thresholds_out[logical] = (uint16_t) threshold;

		if ((uint16_t) threshold < threshold_min) {
			threshold_min = (uint16_t) threshold;
		}
		if ((uint16_t) threshold > threshold_max) {
			threshold_max = (uint16_t) threshold;
		}
	}

	flash_write(Flash.raw_flash);

	if (min_threshold_out != NULL) {
		*min_threshold_out = threshold_min;
	}
	if (max_threshold_out != NULL) {
		*max_threshold_out = threshold_max;
	}

	return 1;
}

void capsense_uart_stats_get(capsense_uart_stats_t *stats_out)
{
	if (stats_out == NULL) {
		return;
	}

	*stats_out = capsense_uart_stats;
	stats_out->protocol_version = capsense_procotl_version;
	stats_out->legacy_payload_offset = capsense_legacy_payload_offset;
}

void capsense_uart_stats_reset(void)
{
	memset(&capsense_uart_stats, 0, sizeof(capsense_uart_stats));
	capsense_uart_stats.protocol_version = capsense_procotl_version;
	capsense_uart_stats.legacy_payload_offset = capsense_legacy_payload_offset;
}

void capsense_uart_stats_note_short_packet(void)
{
	capsense_uart_stats.short_packet_count++;
}

void capsense_uart_stats_note_empty_packet(void)
{
	capsense_uart_stats.empty_packet_count++;
}

void capsense_uart_stats_note_parse_fail(void)
{
	capsense_uart_stats.parse_fail_count++;
	capsense_last_error_tick = HAL_GetTick();
}

void capsense_uart_stats_note_uart_error(void)
{
	capsense_uart_stats.uart_error_count++;
	capsense_last_error_tick = HAL_GetTick();
}

void capsense_uart_stats_note_auto_reset(void)
{
	capsense_uart_stats.auto_reset_count++;
	capsense_last_error_tick = HAL_GetTick();
}

void capsense_uart_stats_set_failure_streak(uint8_t streak)
{
	capsense_uart_stats.rx_failure_streak = streak;
	capsense_uart_stats.protocol_version = capsense_procotl_version;
	capsense_uart_stats.legacy_payload_offset = capsense_legacy_payload_offset;
}

void capsense_link_state_get(uint32_t *last_good_tick_out, uint32_t *last_error_tick_out, uint8_t *protocol_version_out)
{
	if (last_good_tick_out != NULL) {
		*last_good_tick_out = capsense_last_good_frame_tick;
	}
	if (last_error_tick_out != NULL) {
		*last_error_tick_out = capsense_last_error_tick;
	}
	if (protocol_version_out != NULL) {
		*protocol_version_out = capsense_procotl_version;
	}
}

void capsense_request_link_reset(void)
{
	capsense_reset_pending = 1;
}

void capsense_service_pending_reset(void)
{
	uint32_t primask;

	if (capsense_reset_pending == 0u) {
		return;
	}

	primask = __get_PRIMASK();
	__disable_irq();
	if (capsense_reset_pending != 0u) {
		capsense_reset_pending = 0;
	}
	if (primask == 0u) {
		__enable_irq();
	}

	(void) HAL_UART_DMAStop(&huart4);
	Boot_Buttom_IRQHandler();
	capsense_restart_uart4_rx();
}
