/*
 * capsense_calibration.c
 *
 * Automatic threshold and guided touch mapping calibration.
 */

#include "capsense_internal.h"
#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "main.h"
#include "string.h"

static uint16_t capsense_calibration_press_samples[34][CAPSENSE_CALIBRATION_PRESS_HOLD_FRAMES];
static uint16_t capsense_calibration_press_sorted[CAPSENSE_CALIBRATION_PRESS_HOLD_FRAMES];
static uint16_t capsense_calibration_threshold_stage[34];
static uint8_t capsense_calibration_mapping_stage[34];
static uint8_t capsense_calibration_locked_channel_for_logical[34];
static uint8_t capsense_calibration_channel_lock_owner[34];
static uint8_t capsense_calibration_active = 0;
static volatile uint8_t capsense_calibration_cancel_requested = 0u;

static void capsense_calibration_reset_channel_locks(void)
{
	memset(capsense_calibration_locked_channel_for_logical, 0xFF,
			sizeof(capsense_calibration_locked_channel_for_logical));
	memset(capsense_calibration_channel_lock_owner, 0xFF,
			sizeof(capsense_calibration_channel_lock_owner));
}

static uint8_t capsense_calibration_channel_locked_for_other_logical(
		uint8_t logical_index, uint8_t channel)
{
	uint8_t owner = capsense_calibration_channel_lock_owner[channel];

	return (uint8_t) ((owner != 0xFFu) && (owner != logical_index));
}

static void capsense_calibration_lock_channel(uint8_t logical_index, uint8_t channel)
{
	uint8_t previous_channel = capsense_calibration_locked_channel_for_logical[logical_index];

	if ((previous_channel < 34u) &&
			(capsense_calibration_channel_lock_owner[previous_channel] == logical_index)) {
		capsense_calibration_channel_lock_owner[previous_channel] = 0xFFu;
	}

	capsense_calibration_locked_channel_for_logical[logical_index] = channel;
	capsense_calibration_channel_lock_owner[channel] = logical_index;
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

void capsense_calibration_begin(void)
{
	memcpy(capsense_calibration_threshold_stage, Flash.touch_threshold,
			sizeof(capsense_calibration_threshold_stage));
	memcpy(capsense_calibration_mapping_stage, Flash.touch_sheet,
			sizeof(capsense_calibration_mapping_stage));
	capsense_calibration_reset_channel_locks();
	capsense_calibration_cancel_requested = 0u;
	capsense_calibration_active = 1u;
}

void capsense_calibration_abort(void)
{
	capsense_calibration_reset_channel_locks();
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

			if (capsense_calibration_channel_locked_for_other_logical(logical_index,
					channel) != 0u) {
				frame_delta[channel] = 0u;
				continue;
			}

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
			if (capsense_calibration_channel_locked_for_other_logical(logical_index,
					channel) != 0u) {
				continue;
			}

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

			capsense_calibration_lock_channel(logical_index, best_channel);
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
	uint16_t previous_thresholds[34];
	uint8_t previous_sheet[34];

	if (capsense_calibration_active == 0u) {
		return 0u;
	}

	memcpy(previous_thresholds, Flash.touch_threshold, sizeof(previous_thresholds));
	memcpy(previous_sheet, Flash.touch_sheet, sizeof(previous_sheet));
	memcpy(Flash.touch_threshold, capsense_calibration_threshold_stage,
			sizeof(capsense_calibration_threshold_stage));
	memcpy(Flash.touch_sheet, capsense_calibration_mapping_stage,
			sizeof(capsense_calibration_mapping_stage));
	if (flash_write(Flash.raw_flash) == 0u) {
		memcpy(Flash.touch_threshold, previous_thresholds, sizeof(previous_thresholds));
		memcpy(Flash.touch_sheet, previous_sheet, sizeof(previous_sheet));
		return 0u;
	}
	capsense_calibration_reset_channel_locks();
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

static uint8_t capsense_auto_calibrate_threshold_batch(uint8_t logical_start,
		uint8_t logical_count, capsense_auto_threshold_workspace_t *workspace,
		uint16_t *thresholds_out, uint16_t *threshold_min_io,
		uint16_t *threshold_max_io)
{
	uint16_t min_raw[CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS];
	uint16_t max_raw[CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS];
	uint16_t max_positive_delta[CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS];
	uint16_t raw_frame[CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS];
	uint16_t positive_delta_frame[CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS];
	uint16_t prev_raw[CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS];
	uint32_t last_frame_counter;
	uint32_t start_tick;
	uint16_t sample_count = 0;
	uint8_t stable_frames = 0;
	uint8_t prev_raw_valid = 0;
	const uint16_t trim_low_index = (CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT * CAPSENSE_AUTO_THRESHOLD_TRIM_PERCENT) / 100;
	const uint16_t trim_high_index = CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT - 1 - trim_low_index;

	if ((workspace == NULL) || (thresholds_out == NULL) ||
			(threshold_min_io == NULL) || (threshold_max_io == NULL) ||
			(logical_count == 0u) ||
			(logical_count > CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS) ||
			((logical_start + logical_count) > 34u)) {
		return 0u;
	}

	last_frame_counter = capsense_frame_counter;
	start_tick = HAL_GetTick();

	while (sample_count < CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT) {
		uint32_t timeout_ms = CAPSENSE_AUTO_THRESHOLD_FRAME_TIMEOUT_MS;
		uint8_t frame_valid = 1u;

		while (capsense_frame_counter == last_frame_counter) {
			if ((timeout_ms == 0u) ||
					((HAL_GetTick() - start_tick) >= CAPSENSE_AUTO_THRESHOLD_TOTAL_TIMEOUT_MS)) {
				return 0u;
			}
			osDelay(1);
			timeout_ms--;
		}
		last_frame_counter = capsense_frame_counter;

		for (uint8_t logical_offset = 0u; logical_offset < logical_count; logical_offset++) {
			uint8_t logical = (uint8_t) (logical_start + logical_offset);
			uint8_t channel = capsense_channel_for_logical(logical);
			uint16_t raw = Touch.channel_raw[channel];
			uint16_t baseline = capsense_baseline[channel];
			uint16_t positive_delta = raw > baseline ? (uint16_t) (raw - baseline) : 0u;
			uint16_t step_delta = 0u;

			if (prev_raw_valid != 0u) {
				step_delta = raw > prev_raw[logical_offset] ?
						(uint16_t) (raw - prev_raw[logical_offset]) :
						(uint16_t) (prev_raw[logical_offset] - raw);
			}

			if ((capsense_touch_status[logical] != 0u) || (raw >= 0xFF00u) ||
					(positive_delta > CAPSENSE_AUTO_THRESHOLD_BASELINE_DELTA_MAX) ||
					((prev_raw_valid != 0u) && (step_delta > CAPSENSE_AUTO_THRESHOLD_STEP_MAX))) {
				frame_valid = 0u;
				break;
			}

			raw_frame[logical_offset] = raw;
			positive_delta_frame[logical_offset] = positive_delta;
		}

		if (frame_valid == 0u) {
			stable_frames = 0u;
			sample_count = 0u;
			prev_raw_valid = 0u;
			continue;
		}

		memcpy(prev_raw, raw_frame, logical_count * sizeof(prev_raw[0]));
		prev_raw_valid = 1u;

		if (stable_frames < CAPSENSE_AUTO_THRESHOLD_STABLE_FRAMES) {
			stable_frames++;
			continue;
		}

		for (uint8_t logical_offset = 0u; logical_offset < logical_count; logical_offset++) {
			uint16_t raw = raw_frame[logical_offset];
			uint16_t positive_delta = positive_delta_frame[logical_offset];

			workspace->samples[logical_offset][sample_count] = raw;

			if (sample_count == 0u) {
				min_raw[logical_offset] = raw;
				max_raw[logical_offset] = raw;
				max_positive_delta[logical_offset] = positive_delta;
				continue;
			}

			if (raw < min_raw[logical_offset]) {
				min_raw[logical_offset] = raw;
			}
			if (raw > max_raw[logical_offset]) {
				max_raw[logical_offset] = raw;
			}
			if (positive_delta > max_positive_delta[logical_offset]) {
				max_positive_delta[logical_offset] = positive_delta;
			}
		}

		sample_count++;
	}

	for (uint8_t logical_offset = 0u; logical_offset < logical_count; logical_offset++) {
		uint8_t logical = (uint8_t) (logical_start + logical_offset);
		uint32_t peak_to_peak = (uint32_t) max_raw[logical_offset] - (uint32_t) min_raw[logical_offset];
		uint32_t trimmed_span;
		uint32_t threshold;
		uint32_t positive_candidate =
				(uint32_t) max_positive_delta[logical_offset] * CAPSENSE_AUTO_THRESHOLD_POS_MULTIPLIER;

		memcpy(workspace->sorted, workspace->samples[logical_offset],
				sizeof(workspace->sorted));

		for (uint16_t i = 1u; i < CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT; i++) {
			uint16_t value = workspace->sorted[i];
			uint16_t j = i;

			while ((j > 0u) && (workspace->sorted[j - 1u] > value)) {
				workspace->sorted[j] = workspace->sorted[j - 1u];
				j--;
			}
			workspace->sorted[j] = value;
		}

		trimmed_span = (uint32_t) workspace->sorted[trim_high_index] -
				(uint32_t) workspace->sorted[trim_low_index];
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

		if ((uint16_t) threshold < *threshold_min_io) {
			*threshold_min_io = (uint16_t) threshold;
		}
		if ((uint16_t) threshold > *threshold_max_io) {
			*threshold_max_io = (uint16_t) threshold;
		}
	}

	return 1u;
}

uint8_t capsense_auto_calibrate_thresholds(uint16_t *thresholds_out,
		uint16_t *min_threshold_out, uint16_t *max_threshold_out)
{
	capsense_auto_threshold_workspace_t *workspace;
	uint16_t previous_thresholds[34];
	uint16_t threshold_min = 0xFFFFu;
	uint16_t threshold_max = 0u;

	if (thresholds_out == NULL) {
		return 0u;
	}
	memcpy(previous_thresholds, Flash.touch_threshold, sizeof(previous_thresholds));

	workspace = (capsense_auto_threshold_workspace_t *) pvPortMalloc(sizeof(*workspace));
	if (workspace == NULL) {
		return 0u;
	}

	for (uint8_t logical_start = 0u; logical_start < 34u;
			logical_start = (uint8_t) (logical_start + CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS)) {
		uint8_t logical_count = (uint8_t) (34u - logical_start);

		if (logical_count > CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS) {
			logical_count = CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS;
		}

		if (capsense_auto_calibrate_threshold_batch(logical_start, logical_count,
				workspace, thresholds_out, &threshold_min, &threshold_max) == 0u) {
			memcpy(Flash.touch_threshold, previous_thresholds, sizeof(previous_thresholds));
			vPortFree(workspace);
			return 0u;
		}
	}

	vPortFree(workspace);
	if (flash_write(Flash.raw_flash) == 0u) {
		memcpy(Flash.touch_threshold, previous_thresholds, sizeof(previous_thresholds));
		return 0u;
	}

	if (min_threshold_out != NULL) {
		*min_threshold_out = threshold_min;
	}
	if (max_threshold_out != NULL) {
		*max_threshold_out = threshold_max;
	}

	return 1u;
}
