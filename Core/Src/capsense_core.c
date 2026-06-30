/*
 * capsense_core.c
 *
 * Created on: Jan 8, 2025
 * Originally created by Qinh.
 *
 * The current capsense processing and debug architecture
 * has been substantially redesigned and extended by Gl0w1amp.
 *
 * Copyright (c) Ruminasu Labs. All rights reserved.
 */
#include "capsense_internal.h"
#include "debug_mode.h"
#include "capsense_sim.h"
#include "cmsis_os.h"
#include "gpio.h"
#include "input_snapshot.h"
#include "string.h"

uint8_t uart_dma_buffer[128];

packet_capsense_t Touch;
packet_capsense_t capsense_rx_touch;
uint16_t capsense_hold_duration[16] = {0};
uint16_t capsense_freeze[CAPSENSE_CHANNEL_COUNT];
uint16_t capsense_baseline[CAPSENSE_CHANNEL_COUNT];
static uint16_t capsense_hold_prev_raw[16] = {0};
static uint16_t capsense_hold_peak_envelope[16] = {0};
uint16_t capsense_hold_release_level[16] = {0};
static uint8_t capsense_hold_baseline_cooldown[16] = {0};
volatile uint8_t capsense_data_ready = 0;
uint8_t capsense_touch_status[CAPSENSE_CHANNEL_COUNT];
static uint8_t capsense_hold_enter_confirm[16] = {0};
static uint8_t capsense_hold_release_confirm[16] = {0};
static uint8_t capsense_hold_rearm_confirm[16] = {0};
uint8_t capsense_hold_state[16] = {0};
volatile uint8_t capsense_protocol_version = 0;  /* ISR-written, multi-task read */
uint8_t capsense_checksum_last = 0;
uint8_t capsense_legacy_payload_offset = 0;
uint8_t capsense_protocol1_confirm_count = 0;
volatile uint32_t capsense_frame_counter = 0;
capsense_uart_stats_t capsense_uart_stats = {0};
capsense_debug_stats_t capsense_debug_stats = {0};
volatile uint32_t capsense_last_good_frame_tick = 0;
volatile uint32_t capsense_last_real_frame_tick = 0;
volatile uint32_t capsense_last_error_tick = 0;
volatile uint8_t capsense_reset_pending = 0;
uint8_t debug_channel = 0;

static void capsense_reset_runtime_state(void);
uint8_t capsense_channel_for_logical(uint8_t logical_index)
{
	return Flash.touch_sheet[logical_index];
}

static uint16_t capsense_touch_stream_reference_for_logical(uint8_t logical_index)
{
	uint8_t channel = capsense_channel_for_logical(logical_index);
	uint8_t hold_index = capsense_hold_index_for_logical(logical_index);

	if ((hold_index != 0xFFu) &&
			(capsense_hold_state[hold_index] != CAPSENSE_HOLD_STATE_IDLE)) {
		return capsense_freeze[channel];
	}

	return capsense_baseline[channel];
}

static uint16_t capsense_touch_stream_delta_for_logical(uint8_t logical_index)
{
	uint8_t channel = capsense_channel_for_logical(logical_index);
	uint16_t raw = Touch.channel_raw[channel];
	uint16_t reference = capsense_touch_stream_reference_for_logical(logical_index);

	return raw > reference ? (uint16_t) (raw - reference) : 0u;
}



uint16_t capsense_release_threshold(uint16_t enter_threshold)
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

uint8_t capsense_hold_index_for_logical(uint8_t logical_index)
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


void capsense_input_snapshot_publish(void)
{
	uint16_t delta_values[INPUT_SNAPSHOT_TOUCH_CHANNEL_COUNT];
	input_link_state_t link_state = {0};
	uint32_t now = HAL_GetTick();

	for (uint8_t logical = 0u; logical < INPUT_SNAPSHOT_TOUCH_CHANNEL_COUNT; logical++) {
		delta_values[logical] = capsense_touch_stream_delta_for_logical(logical);
	}

	link_state.last_good_tick = capsense_last_good_frame_tick;
	link_state.last_error_tick = capsense_last_error_tick;
	link_state.protocol_version = capsense_protocol_version;
	if (capsense_protocol_version != 0u) {
		link_state.flags |= INPUT_LINK_FLAG_ONLINE;
	}
	if ((capsense_last_error_tick != 0u) &&
			((uint32_t)(now - capsense_last_error_tick) <= 500u)) {
		link_state.flags |= INPUT_LINK_FLAG_ERROR_RECENT;
	}

	input_snapshot_publish_touch(delta_values, capsense_touch_status,
			&link_state, now);
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
			/* Enter on variance-vs-baseline only. The old `|| raw >= 0xFF00`
			 * absolute-saturation override forced touch whenever a channel read
			 * near full-scale regardless of its own baseline. With the PSoC idle
			 * value normalized to ~0xB333 that left only ~30% headroom, so any
			 * common-mode lift / saturated or faulty channel latched all A/D zones
			 * "touched". A genuine touch already yields variance_idle >> enter_threshold,
			 * so real detection is preserved. */
			if(variance_idle > (int) enter_threshold){
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

		/* Release purely on the signal dropping below the release threshold. The
		 * old `&& raw < 0xFF00` clause blocked release while the channel read
		 * saturated, which permanently locked a held A/D contact whenever raw sat
		 * high (the same saturation pathology as the enter path above). */
		if(delta_freeze < release_threshold){
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
		/* Variance-vs-threshold only; the absolute-saturation override was the same
		 * false-latching pathology as in the hold block (see note there). */
		if(variance > Flash.touch_threshold[logical]){
			capsense_touch_status[logical] = 1;
		}
		else{
			capsense_touch_status[logical] = 0;
		}
	}
}

static void capsense_reset_runtime_state(void)
{
	for(uint8_t i = 0;i<CAPSENSE_CHANNEL_COUNT;i++){
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
	capsense_protocol_version = 0;
	capsense_checksum_last = 0;
	capsense_legacy_payload_offset = 0;
	capsense_protocol1_confirm_count = 0;
	capsense_data_ready = 0;
	capsense_uart_stats.protocol_version = 0;
	capsense_uart_stats.legacy_payload_offset = 0;
	capsense_uart_stats.rx_failure_streak = 0;
	memset(&capsense_debug_stats, 0, sizeof(capsense_debug_stats));
	capsense_last_good_frame_tick = 0;
	capsense_last_real_frame_tick = 0;
	capsense_last_error_tick = 0;
	capsense_reset_pending = 0;
	capsense_sim_reset(HAL_GetTick());
	capsense_uart_stream_reset();
}


void capsense_on_boot_button(){
	Board_TouchResetLine_Set(0u);
	capsense_reset_runtime_state();
	Board_TouchResetLine_Set(1u);
}

void capsense_init(){
	capsense_reset_runtime_state();
	if (capsense_restart_uart4_rx() == 0u) {
		capsense_request_link_reset();
	}
	osDelay(100);
	for(uint8_t i = 0;i<CAPSENSE_CHANNEL_COUNT;i++){
		capsense_baseline[i] = Touch.channel_raw[i];
		capsense_freeze[i] = Touch.channel_raw[i];
	}
	for(uint8_t i = 0;i<16;i++){
		capsense_hold_baseline_cooldown[i] = 0;
	}
}

void capsense_check(){
	capsense_process_hold_block(0, 0);
	capsense_process_standard_block(8, 8, CAPSENSE_BASELINE_VARIANCE_B);
	capsense_process_standard_block(16, 2, CAPSENSE_BASELINE_VARIANCE_C);
	capsense_process_hold_block(18, 8);
	capsense_process_standard_block(26, 8, CAPSENSE_BASELINE_VARIANCE_E);
}

/* Link-staleness watchdog. capsense_check() (and therefore the hold-block release
 * state machine) only runs when a fresh PSoC frame was accepted, so a contact that
 * was held at the instant the link went silent (cable pull, PSoC reset, scan stall,
 * sustained checksum failure) would latch "touched" forever while the host keeps
 * re-sending the last snapshot. When no real frame has arrived for
 * CAPSENSE_LINK_STALE_RELEASE_MS, force-release every contact and re-arm the hold
 * machines so the link resumes cleanly. Returns 1 if anything was released (caller
 * should publish). The threshold is well above the PSoC slow-scan period (200ms),
 * and a held contact keeps the PSoC in 30ms fast scan, so this never fires mid-touch. */
uint8_t capsense_handle_link_stale(uint32_t now)
{
	uint8_t released = 0;

	if ((uint32_t)(now - capsense_last_real_frame_tick) < CAPSENSE_LINK_STALE_RELEASE_MS) {
		return 0;
	}

	for (uint8_t logical = 0; logical < CAPSENSE_CHANNEL_COUNT; logical++) {
		if (capsense_touch_status[logical] != 0) {
			capsense_touch_status[logical] = 0;
			released = 1;
		}
	}

	if (released) {
		/* Clear the full per-hold machine state (matching capsense_reset_runtime_state's
		 * hold loop) so the link resumes from a pristine IDLE and no stale rearm-inhibit,
		 * cooldown, peak-envelope or release-level carries over. The learned
		 * capsense_baseline / capsense_freeze are deliberately kept so touch sensing does
		 * not have to re-settle after a brief link blip. */
		for (uint8_t i = 0; i < 16; i++) {
			capsense_hold_state[i] = CAPSENSE_HOLD_STATE_IDLE;
			capsense_hold_duration[i] = 0;
			capsense_hold_enter_confirm[i] = 0;
			capsense_hold_release_confirm[i] = 0;
			capsense_hold_rearm_confirm[i] = 0;
			capsense_hold_baseline_cooldown[i] = 0;
			capsense_hold_prev_raw[i] = 0;
			capsense_hold_peak_envelope[i] = 0;
			capsense_hold_release_level[i] = 0;
		}
	}

	return released;
}



