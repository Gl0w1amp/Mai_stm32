/*
 * capsense_debug.h
 *
 * Capsense debug sub-header. Split out of capsense.h; re-included by
 * capsense.h so existing consumers compile with zero call-site churn.
 */

#ifndef INC_CAPSENSE_DEBUG_H_
#define INC_CAPSENSE_DEBUG_H_

#include <stdint.h>

typedef struct {
	uint32_t service_call_count;
	uint32_t emit_batch_count;
	uint32_t enqueue_attempt_count;
	uint32_t enqueue_success_count;
	uint8_t last_queue_slots;
	uint8_t last_debug_flag;
	uint8_t last_debug_stream_mode;
	uint8_t last_enqueue_success_mask;
} capsense_debug_stats_t;

void capsense_debug_service(void);
void capsense_debug_stats_get(capsense_debug_stats_t *stats_out);
void capsense_debug_stats_reset(void);

#endif /* INC_CAPSENSE_DEBUG_H_ */
