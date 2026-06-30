/*
 * capsense_uart.c
 *
 * PSoC capsense UART frame receive, parse, link-state, and statistics handling.
 */

#include "capsense_internal.h"
#include "capsense_sim.h"
#include "usart.h"
#include "cmsis_os.h"
#include "serial_checksum.h"
#include "critical_section.h"
#include "string.h"

extern DMA_HandleTypeDef hdma_uart4_rx;

static bool capsense_data_proc(uint8_t *uart_dma_buffer);
static bool capsense_data_proc_legacy(uint8_t *uart_dma_buffer);

static uint8_t capsense_uart_stream_buffer[CAPSENSE_UART_STREAM_BUFFER_SIZE];
static uint16_t capsense_uart_stream_head = 0;
static uint16_t capsense_uart_stream_tail = 0;

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

static uint8_t capsense_accept_packet(const uint8_t *data, uint8_t lock_protocol, uint8_t rolling_checksum)
{
	memcpy(&capsense_rx_touch.data[0], data + 1, 68);
	if(lock_protocol){
		capsense_protocol_version = 1;
	}
	if (rolling_checksum) {
		capsense_uart_stats.rolling_checksum_accept_count++;
	} else {
		capsense_uart_stats.checksum_accept_count++;
	}
	capsense_last_real_frame_tick = HAL_GetTick();
	capsense_last_good_frame_tick = capsense_last_real_frame_tick;
	capsense_frame_counter++;
	capsense_data_ready = 1;
	return 1;
}

static uint8_t capsense_accept_legacy_packet(const uint8_t *data, uint8_t payload_offset)
{
	memcpy(&capsense_rx_touch.data[0], data + payload_offset, 68);
	if(capsense_protocol_version == 0){
		capsense_protocol_version = 2;
	}
	capsense_uart_stats.legacy_accept_count++;
	capsense_legacy_payload_offset = payload_offset;
	capsense_protocol1_confirm_count = 0;
	capsense_last_real_frame_tick = HAL_GetTick();
	capsense_last_good_frame_tick = capsense_last_real_frame_tick;
	capsense_frame_counter++;
	capsense_data_ready = 1;
	return 1;
}

static uint8_t capsense_score_legacy_payload(const uint8_t *data, uint8_t payload_offset)
{
	uint8_t low_nibble_zero_count = 0;
	uint8_t non_zero_count = 0;

	for (uint8_t i = 0; i < CAPSENSE_CHANNEL_COUNT; i++) {
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

#define CAPSENSE_CONSECUTIVE_FAILURE_RESET_THRESHOLD 8u
#define CAPSENSE_RESET_COOLDOWN_MS 500u

static uint8_t capsense_rx_failure_count = 0;
static uint32_t capsense_last_reset_tick = 0;

static void capsense_uart_note_rx_failure(void)
{
    uint32_t now = HAL_GetTick();

    if (capsense_rx_failure_count < 0xFFu) {
        capsense_rx_failure_count++;
    }
    capsense_uart_stats_set_failure_streak(capsense_rx_failure_count);

    if (capsense_rx_failure_count < CAPSENSE_CONSECUTIVE_FAILURE_RESET_THRESHOLD) {
        return;
    }

    if ((uint32_t)(now - capsense_last_reset_tick) < CAPSENSE_RESET_COOLDOWN_MS) {
        return;
    }

    capsense_last_reset_tick = now;
    capsense_rx_failure_count = 0;
    capsense_uart_stats_note_auto_reset();
    capsense_uart_stats_set_failure_streak(capsense_rx_failure_count);
    capsense_request_link_reset();
}

void capsense_uart_on_rx_result(uint16_t accepted_frames, uint16_t rejected_frames)
{
    if (accepted_frames > 0u) {
        capsense_rx_failure_count = 0;
        capsense_uart_stats_set_failure_streak(capsense_rx_failure_count);
    } else if (rejected_frames > 0u) {
        capsense_uart_note_rx_failure();
    }
}

void capsense_uart_on_error(void)
{
    capsense_uart_stats_note_uart_error();
    capsense_uart_note_rx_failure();
}

static bool capsense_data_proc(uint8_t *uart_dma_buffer){
	uint8_t strict_checksum;
	uint8_t rolling_checksum;

	if((capsense_protocol_version != 0) && (capsense_protocol_version != 1)){
		return false;
	}
    if(uart_dma_buffer[0] == 0){
		if (capsense_protocol_version == 0) {
			uint8_t legacy_score_offset_2 = capsense_score_legacy_payload(uart_dma_buffer, 2);
			uint8_t legacy_score_offset_1 = capsense_score_legacy_payload(uart_dma_buffer, 1);

			if ((uart_dma_buffer[1] == 0u) &&
					(legacy_score_offset_2 >= 24u) &&
					(legacy_score_offset_2 >= (uint8_t) (legacy_score_offset_1 + 4u))) {
				capsense_protocol1_confirm_count = 0;
				return false;
			}
		}

		strict_checksum = serial_checksum_sum(uart_dma_buffer, 69u);
		if(strict_checksum == uart_dma_buffer[69]){
			capsense_checksum_last = uart_dma_buffer[69];
			if (capsense_protocol_version == 1) {
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
			if (capsense_protocol_version == 1) {
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

static bool capsense_data_proc_legacy(uint8_t *uart_dma_buffer){
	uint8_t payload_offset;

	if((capsense_protocol_version != 0) && (capsense_protocol_version != 2)){
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
	uint32_t primask;
	capsense_sim_context_t sim_context = {
		.rx_touch = &capsense_rx_touch,
		.data_ready = &capsense_data_ready,
		.last_good_frame_tick = &capsense_last_good_frame_tick,
		.frame_counter = &capsense_frame_counter,
		.last_real_frame_tick = capsense_last_real_frame_tick,
		.protocol_version = &capsense_protocol_version,
		.uart_stats = &capsense_uart_stats,
		.logical_to_channel = Flash.touch_sheet,
	};

	capsense_sim_maybe_generate(&sim_context, HAL_GetTick());

	primask = critical_section_enter();
	if (capsense_data_ready) {
		memcpy(&Touch, &capsense_rx_touch, sizeof(Touch));
		capsense_data_ready = 0;
		snapshot_ready = 1;
	}
	critical_section_exit(primask);

	return snapshot_ready;
}


uint8_t capsense_restart_uart4_rx(void)
{
	HAL_StatusTypeDef status;

	(void) HAL_UART_DMAStop(&huart4);
	__HAL_UART_CLEAR_IT(&huart4,
			UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF | UART_CLEAR_PEF);
	status = HAL_UARTEx_ReceiveToIdle_DMA(&huart4, uart_dma_buffer, 128);
	if (status != HAL_OK) {
		capsense_uart_stats_note_uart_error();
		return 0u;
	}
	__HAL_DMA_DISABLE_IT(&hdma_uart4_rx, DMA_IT_HT);
	return 1u;
}

void capsense_uart_stats_get(capsense_uart_stats_t *stats_out)
{
	uint32_t primask;

	if (stats_out == NULL) {
		return;
	}

	/* Snapshot consistently: the UART4 RX ISR increments these counters, so an
	 * unguarded whole-struct copy could mix pre/post-increment fields. */
	primask = critical_section_enter();
	*stats_out = capsense_uart_stats;
	stats_out->protocol_version = capsense_protocol_version;
	stats_out->legacy_payload_offset = capsense_legacy_payload_offset;
	critical_section_exit(primask);
}

void capsense_uart_stats_reset(void)
{
	/* Guard against the UART4 RX ISR that increments these counters (same race the
	 * getter guards). Also clear the live consecutive-failure counter: the reported
	 * streak is derived from this static, not from the struct, so without this a
	 * "reset stats" would leave the streak able to snap back on the next failure. */
	uint32_t primask = critical_section_enter();
	memset(&capsense_uart_stats, 0, sizeof(capsense_uart_stats));
	capsense_uart_stats.protocol_version = capsense_protocol_version;
	capsense_uart_stats.legacy_payload_offset = capsense_legacy_payload_offset;
	capsense_rx_failure_count = 0u;
	critical_section_exit(primask);
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
	capsense_uart_stats.protocol_version = capsense_protocol_version;
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
		*protocol_version_out = capsense_protocol_version;
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

	primask = critical_section_enter();
	if (capsense_reset_pending != 0u) {
		capsense_reset_pending = 0;
	}
	critical_section_exit(primask);

	/* Serialize this non-reentrant DMAStop + ReceiveToIdle sequence against the UART4
	 * RX-event/error and RX-DMA (DMA2_Channel1) ISRs, which re-arm the same huart4
	 * handle. Mask at the NVIC (not PRIMASK) so the RTOS SysTick/scheduler and any
	 * blocking HAL poll stay live during the sequence. Clear any stale pending edge
	 * that latched while masked before re-enabling, so it cannot re-arm on top of the
	 * freshly-started receive. */
	HAL_NVIC_DisableIRQ(UART4_IRQn);
	HAL_NVIC_DisableIRQ(DMA2_Channel1_IRQn);

	(void) HAL_UART_DMAStop(&huart4);
	capsense_on_boot_button();
	if (capsense_restart_uart4_rx() == 0u) {
		capsense_request_link_reset();
	}

	HAL_NVIC_ClearPendingIRQ(UART4_IRQn);
	HAL_NVIC_ClearPendingIRQ(DMA2_Channel1_IRQn);
	HAL_NVIC_EnableIRQ(UART4_IRQn);
	HAL_NVIC_EnableIRQ(DMA2_Channel1_IRQn);
}
