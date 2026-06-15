/*
 * capsense_sim.h
 *
 * Optional PSoC capsense frame simulator.
 */

#ifndef INC_CAPSENSE_SIM_H_
#define INC_CAPSENSE_SIM_H_

#include "capsense.h"
#include <stdint.h>

#ifndef CAPSENSE_SIMULATED_TOUCH_ENABLE
#define CAPSENSE_SIMULATED_TOUCH_ENABLE 0u
#endif

typedef struct {
	packet_capsense_t *rx_touch;
	volatile uint8_t *data_ready;
	volatile uint32_t *last_good_frame_tick;
	volatile uint32_t *frame_counter;
	uint32_t last_real_frame_tick;
	uint8_t *protocol_version;
	capsense_uart_stats_t *uart_stats;
	const uint8_t *logical_to_channel;
} capsense_sim_context_t;

void capsense_sim_reset(uint32_t now);
void capsense_sim_maybe_generate(const capsense_sim_context_t *ctx,
		uint32_t now);

#endif /* INC_CAPSENSE_SIM_H_ */
