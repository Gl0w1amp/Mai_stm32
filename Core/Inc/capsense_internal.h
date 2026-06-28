/*

 * capsense_internal.h

 *

 * Internal state and helper declarations shared by capsense modules.

 */

#ifndef INC_CAPSENSE_INTERNAL_H_

#define INC_CAPSENSE_INTERNAL_H_

#include "capsense.h"

#include "flash.h"

#include "debug_mode.h"

#include <stdint.h>

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

#define CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS 17

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

#define CAPSENSE_DEBUG_FOCUS_FLOAT_COUNT 6u

#define CAPSENSE_DEBUG_RAW_FLOAT_COUNT 34u

#define CAPSENSE_DEBUG_STREAM_CHUNK_FLOAT_COUNT 15u

#define CAPSENSE_DEBUG_VOFA_TAIL_SIZE 4u

#define CAPSENSE_DEBUG_RAW_MIN_INTERVAL_MS 12u

#define CAPSENSE_DEBUG_RAW_PACKET_COUNT 3u

#define CAPSENSE_UART_FRAME_SIZE 70u

#define CAPSENSE_UART_STREAM_BUFFER_SIZE 512u

typedef enum {

	CAPSENSE_HOLD_STATE_IDLE = 0,

	CAPSENSE_HOLD_STATE_ACTIVE = 1,

	CAPSENSE_HOLD_STATE_RELEASE_CONFIRM = 2

} capsense_hold_state_t;

typedef union {

	float raw_data_fl[CAPSENSE_DEBUG_STREAM_CHUNK_FLOAT_COUNT];

	uint8_t raw_data_u8[CAPSENSE_DEBUG_STREAM_CHUNK_FLOAT_COUNT * sizeof(float)];

} vofa_debug_chunk_t;

typedef struct {

	uint16_t samples[CAPSENSE_AUTO_THRESHOLD_BATCH_CHANNELS][CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT];

	uint16_t sorted[CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT];

} capsense_auto_threshold_workspace_t;

extern packet_capsense_t capsense_rx_touch;

extern uint16_t capsense_hold_duration[16];

extern uint16_t capsense_level[8];

extern uint16_t capsense_freeze[34];

extern uint16_t capsense_baseline[34];

extern uint16_t capsense_hold_prev_raw[16];

extern uint16_t capsense_hold_peak_envelope[16];

extern uint16_t capsense_hold_release_level[16];

extern uint8_t capsense_hold_baseline_cooldown[16];

extern uint8_t capsense_bit;

extern uint8_t capsense_hold_enter_confirm[16];

extern uint8_t capsense_hold_release_confirm[16];

extern uint8_t capsense_hold_rearm_confirm[16];

extern uint8_t capsense_hold_state[16];

extern uint8_t capsense_procotl_version;

extern uint8_t capsense_checksum_last;

extern uint8_t capsense_legacy_payload_offset;

extern uint8_t capsense_protocol1_confirm_count;

extern volatile uint32_t capsense_frame_counter;

extern capsense_uart_stats_t capsense_uart_stats;

extern capsense_debug_stats_t capsense_debug_stats;

extern volatile uint32_t capsense_last_good_frame_tick;

extern volatile uint32_t capsense_last_real_frame_tick;

extern volatile uint32_t capsense_last_error_tick;

extern volatile uint8_t capsense_reset_pending;

uint8_t capsense_channel_for_logical(uint8_t logical_index);

uint8_t capsense_hold_index_for_logical(uint8_t logical_index);

uint16_t capsense_release_threshold(uint16_t enter_threshold);

uint8_t capsense_restart_uart4_rx(void);

#endif /* INC_CAPSENSE_INTERNAL_H_ */

