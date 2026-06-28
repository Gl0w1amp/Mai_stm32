/*
 * capsense_calibration.h
 *
 * Capsense calibration sub-header. Split out of capsense.h; re-included by
 * capsense.h so existing consumers compile with zero call-site churn.
 */

#ifndef INC_CAPSENSE_CALIBRATION_H_
#define INC_CAPSENSE_CALIBRATION_H_

#include <stdint.h>

typedef struct {
	uint8_t logical_index;
	uint8_t best_channel;
	uint8_t confidence;
	uint16_t threshold;
	uint16_t peak_delta;
	uint16_t idle_threshold;
} capsense_calibration_result_t;

#define CAPSENSE_CALIBRATION_CAPTURE_FLAG_RELAXED 0x01u

uint8_t capsense_auto_calibrate_thresholds(uint16_t *thresholds_out, uint16_t *min_threshold_out, uint16_t *max_threshold_out);
void capsense_calibration_begin(void);
void capsense_calibration_abort(void);
void capsense_calibration_request_cancel(void);
uint8_t capsense_calibration_capture(uint8_t logical_index, uint8_t capture_flags, capsense_calibration_result_t *result_out);
uint8_t capsense_calibration_commit(void);

#endif /* INC_CAPSENSE_CALIBRATION_H_ */
