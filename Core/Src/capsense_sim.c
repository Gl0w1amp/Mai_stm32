/*
 * capsense_sim.c
 *
 * Optional generated capsense frames for host-side HID/CDC testing.
 */

#include "capsense_sim.h"

#include <stddef.h>

#define CAPSENSE_SIMULATED_START_DELAY_MS 1000u
#define CAPSENSE_SIMULATED_REAL_LINK_HOLD_MS 250u
#define CAPSENSE_SIMULATED_FRAME_INTERVAL_MS 5u
#define CAPSENSE_SIMULATED_BASELINE 1800u
#define CAPSENSE_SIMULATED_WAVE_DELTA 120u
#define CAPSENSE_SIMULATED_TOUCH_DELTA_MIN 2300u
#define CAPSENSE_SIMULATED_TOUCH_DELTA_SPAN 1800u
#define CAPSENSE_SIMULATED_SECONDARY_DELTA 2600u
#define CAPSENSE_SIMULATED_EDGE_DELTA 2300u
#define CAPSENSE_SIMULATED_WARMUP_MS 500u
#define CAPSENSE_SIMULATED_TOUCH_STEP_MS 1200u
#define CAPSENSE_SIMULATED_WAVE_STEP_MS 18u
#define CAPSENSE_SIMULATED_BUTTON_STEP_MS 900u
#define CAPSENSE_SIMULATED_BUTTON_HOLD_MS 560u

typedef struct {
	uint8_t buttons0;
	uint8_t buttons1;
} capsense_sim_button_pattern_t;

static uint32_t capsense_sim_boot_tick = 0u;
static uint32_t capsense_sim_last_emit_tick = 0u;
static uint32_t capsense_sim_rng = 0x51A7C0DEu;

#if CAPSENSE_SIMULATED_TOUCH_ENABLE
static const capsense_sim_button_pattern_t capsense_sim_button_patterns[] = {
	{0x01u, 0x00u},
	{0x02u, 0x00u},
	{0x04u, 0x00u},
	{0x08u, 0x00u},
	{0x10u, 0x00u},
	{0x20u, 0x00u},
	{0x40u, 0x00u},
	{0x80u, 0x00u},
	{0x03u, 0x00u},
	{0x00u, 0x01u},
	{0x00u, 0x02u},
	{0x00u, 0x04u},
	{0x00u, 0x08u},
	{0x00u, 0x10u},
	{0x00u, 0x20u},
	{0x00u, 0x09u},
};

static uint16_t capsense_sim_triangle(uint32_t phase, uint16_t amplitude)
{
	uint32_t period = 64u;
	uint32_t half_period = period / 2u;
	uint32_t position = phase % period;
	uint32_t ramp = position < half_period ? position : (period - position);

	return (uint16_t)((ramp * amplitude) / half_period);
}

static uint16_t capsense_sim_next_noise(uint16_t amplitude)
{
	uint32_t x = capsense_sim_rng;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	capsense_sim_rng = x;

	return (uint16_t)(x % ((uint32_t)amplitude + 1u));
}

static uint8_t capsense_sim_channel_for_logical(
		const capsense_sim_context_t *ctx, uint8_t logical)
{
	if ((ctx == NULL) || (ctx->logical_to_channel == NULL) || (logical >= 34u)) {
		return 0xFFu;
	}
	return ctx->logical_to_channel[logical];
}

static void capsense_sim_add_logical_touch(const capsense_sim_context_t *ctx,
		uint8_t logical, uint16_t delta)
{
	uint8_t channel = capsense_sim_channel_for_logical(ctx, logical);

	if (channel < 34u) {
		uint32_t raw = (uint32_t)ctx->rx_touch->channel_raw[channel] + delta;
		ctx->rx_touch->channel_raw[channel] =
				raw > 0xFE00u ? 0xFE00u : (uint16_t)raw;
	}
}

static void capsense_sim_generate_frame(const capsense_sim_context_t *ctx,
		uint32_t now)
{
	uint32_t elapsed = now - capsense_sim_boot_tick;
	uint8_t warmup_complete = (uint8_t)(elapsed >=
			(CAPSENSE_SIMULATED_START_DELAY_MS + CAPSENSE_SIMULATED_WARMUP_MS));

	for (uint8_t channel = 0u; channel < 34u; channel++) {
		uint32_t phase = (now / CAPSENSE_SIMULATED_WAVE_STEP_MS) +
				(channel * 5u);
		uint32_t raw = CAPSENSE_SIMULATED_BASELINE +
				capsense_sim_triangle(phase, CAPSENSE_SIMULATED_WAVE_DELTA) +
				capsense_sim_next_noise(48u);

		ctx->rx_touch->channel_raw[channel] =
				raw > 0xFE00u ? 0xFE00u : (uint16_t)raw;
	}

	if (warmup_complete != 0u) {
		uint32_t active_elapsed = elapsed - CAPSENSE_SIMULATED_START_DELAY_MS -
				CAPSENSE_SIMULATED_WARMUP_MS;
		uint32_t step = active_elapsed / CAPSENSE_SIMULATED_TOUCH_STEP_MS;
		uint8_t ring = (uint8_t)(step % 8u);
		uint8_t ring_next = (uint8_t)((ring + 1u) % 8u);
		uint32_t pulse_phase = ((active_elapsed %
				CAPSENSE_SIMULATED_TOUCH_STEP_MS) * 64u) /
				CAPSENSE_SIMULATED_TOUCH_STEP_MS;
		uint16_t pulse = (uint16_t)(CAPSENSE_SIMULATED_TOUCH_DELTA_MIN +
				capsense_sim_triangle(pulse_phase,
						CAPSENSE_SIMULATED_TOUCH_DELTA_SPAN) +
				capsense_sim_next_noise(220u));

		capsense_sim_add_logical_touch(ctx, ring, pulse);
		capsense_sim_add_logical_touch(ctx, ring_next,
				(uint16_t)(CAPSENSE_SIMULATED_SECONDARY_DELTA +
						capsense_sim_next_noise(180u)));
		capsense_sim_add_logical_touch(ctx, (uint8_t)(8u + ring),
				(uint16_t)(CAPSENSE_SIMULATED_SECONDARY_DELTA +
						capsense_sim_next_noise(160u)));
		capsense_sim_add_logical_touch(ctx, (uint8_t)(16u + (step & 1u)),
				(uint16_t)(CAPSENSE_SIMULATED_EDGE_DELTA +
						capsense_sim_next_noise(120u)));
		capsense_sim_add_logical_touch(ctx, (uint8_t)(18u + ring),
				(uint16_t)(pulse - 240u));
		capsense_sim_add_logical_touch(ctx, (uint8_t)(18u + ring_next),
				(uint16_t)(CAPSENSE_SIMULATED_SECONDARY_DELTA +
						capsense_sim_next_noise(180u)));
		capsense_sim_add_logical_touch(ctx, (uint8_t)(26u + ((ring + 4u) % 8u)),
				(uint16_t)(CAPSENSE_SIMULATED_EDGE_DELTA +
						capsense_sim_next_noise(120u)));
	}
}
#endif

void capsense_sim_reset(uint32_t now)
{
	capsense_sim_boot_tick = now;
	capsense_sim_last_emit_tick = 0u;
	capsense_sim_rng = 0x51A7C0DEu ^ now;
}

void capsense_sim_maybe_generate(const capsense_sim_context_t *ctx,
		uint32_t now)
{
#if CAPSENSE_SIMULATED_TOUCH_ENABLE
	if ((ctx == NULL) || (ctx->rx_touch == NULL) ||
			(ctx->data_ready == NULL) ||
			(ctx->last_good_frame_tick == NULL) ||
			(ctx->frame_counter == NULL) ||
			(ctx->protocol_version == NULL) ||
			(ctx->uart_stats == NULL)) {
		return;
	}
	if (*ctx->data_ready != 0u) {
		return;
	}
	if ((capsense_sim_boot_tick == 0u) ||
			((uint32_t)(now - capsense_sim_boot_tick) <
					CAPSENSE_SIMULATED_START_DELAY_MS)) {
		return;
	}
	if ((ctx->last_real_frame_tick != 0u) &&
			((uint32_t)(now - ctx->last_real_frame_tick) <=
					CAPSENSE_SIMULATED_REAL_LINK_HOLD_MS)) {
		return;
	}
	if ((capsense_sim_last_emit_tick != 0u) &&
			((uint32_t)(now - capsense_sim_last_emit_tick) <
					CAPSENSE_SIMULATED_FRAME_INTERVAL_MS)) {
		return;
	}

	capsense_sim_generate_frame(ctx, now);
	capsense_sim_last_emit_tick = now;
	*ctx->last_good_frame_tick = now;
	(*ctx->frame_counter)++;
	*ctx->data_ready = 1u;
	if (*ctx->protocol_version == 0u) {
		*ctx->protocol_version = 1u;
	}
	ctx->uart_stats->protocol_version = *ctx->protocol_version;
#else
	(void)ctx;
	(void)now;
#endif
}

void capsense_sim_maybe_generate_buttons(uint8_t *button_bits, uint32_t now)
{
#if CAPSENSE_SIMULATED_TOUCH_ENABLE
	uint32_t elapsed;
	uint32_t step;
	uint32_t within_step;
	const capsense_sim_button_pattern_t *pattern;

	if ((button_bits == NULL) || (capsense_sim_boot_tick == 0u) ||
			((uint32_t)(now - capsense_sim_boot_tick) <
					CAPSENSE_SIMULATED_START_DELAY_MS)) {
		return;
	}

	elapsed = now - capsense_sim_boot_tick - CAPSENSE_SIMULATED_START_DELAY_MS;
	step = elapsed / CAPSENSE_SIMULATED_BUTTON_STEP_MS;
	within_step = elapsed % CAPSENSE_SIMULATED_BUTTON_STEP_MS;
	if (within_step >= CAPSENSE_SIMULATED_BUTTON_HOLD_MS) {
		return;
	}

	pattern = &capsense_sim_button_patterns[step %
			(sizeof(capsense_sim_button_patterns) /
					sizeof(capsense_sim_button_patterns[0]))];
	button_bits[0] |= pattern->buttons0;
	button_bits[1] |= pattern->buttons1;
#else
	(void)button_bits;
	(void)now;
#endif
}

uint8_t capsense_sim_is_enabled(void)
{
#if CAPSENSE_SIMULATED_TOUCH_ENABLE
	return 1u;
#else
	return 0u;
#endif
}
