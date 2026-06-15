/*
 * input_snapshot.h
 *
 * Shared input state published by sampling code and consumed by USB reports.
 */

#ifndef INC_INPUT_SNAPSHOT_H_
#define INC_INPUT_SNAPSHOT_H_

#include <stdint.h>

#define INPUT_SNAPSHOT_TOUCH_CHANNEL_COUNT 34u
#define INPUT_SNAPSHOT_TOUCH_BITS_SIZE 5u
#define INPUT_SNAPSHOT_BUTTON_BITS_SIZE 2u

#define INPUT_LINK_FLAG_ONLINE 0x01u
#define INPUT_LINK_FLAG_ERROR_RECENT 0x02u

typedef struct {
	uint32_t last_good_tick;
	uint32_t last_error_tick;
	uint8_t protocol_version;
	uint8_t flags;
} input_link_state_t;

typedef struct {
	uint32_t seq;
	uint32_t tick_ms;
	uint16_t touch_strength[INPUT_SNAPSHOT_TOUCH_CHANNEL_COUNT];
	uint8_t touch_bits[INPUT_SNAPSHOT_TOUCH_BITS_SIZE];
	uint8_t button_bits[INPUT_SNAPSHOT_BUTTON_BITS_SIZE];
	input_link_state_t link;
} input_snapshot_t;

void input_snapshot_reset(void);
void input_snapshot_publish_touch(const uint16_t *strength_values,
		const uint8_t *touch_status, const input_link_state_t *link_state,
		uint32_t tick_ms);
void input_snapshot_publish_buttons(const uint8_t *button_bits, uint32_t tick_ms);
uint8_t input_snapshot_get_latest(input_snapshot_t *snapshot_out);

#endif /* INC_INPUT_SNAPSHOT_H_ */
