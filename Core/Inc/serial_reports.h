/*
 * serial_reports.h
 *
 * CDC/serial report encoders.
 */

#ifndef INC_SERIAL_REPORTS_H_
#define INC_SERIAL_REPORTS_H_

#include "input_snapshot.h"
#include <stdint.h>

uint8_t serial_reports_checksum(const uint8_t *buf, uint8_t len);
uint8_t serial_reports_build_live_state_frame(uint8_t command,
		const input_snapshot_t *snapshot, uint8_t *buf, uint8_t *len_out);
uint8_t serial_reports_build_touch_scan_frame(const input_snapshot_t *snapshot,
		uint8_t *buf, uint8_t *len_out);

#endif /* INC_SERIAL_REPORTS_H_ */
