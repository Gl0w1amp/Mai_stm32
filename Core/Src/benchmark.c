/*
 * benchmark.c
 *
 * DWT-based benchmark/cycle-counter subsystem.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "main.h"
#include "usb_reporter.h"
#include "usbd_hid_custom_if.h"
#include "serial_protocol.h"
#include "benchmark.h"

#define BENCHMARK_EVENT_PAYLOAD 8
#define BENCHMARK_EVENT_REPLY_PAYLOAD 24
#define BENCHMARK_EVENT_DELAY_MS_DEFAULT 10
#define BENCHMARK_QUIET_PERIOD_MS 30

volatile uint32_t benchmark_quiet_until_ms = 0;
volatile uint8_t benchmark_event_pending = 0;
volatile uint32_t benchmark_event_due_ms = 0;
volatile uint32_t benchmark_event_sequence = 0;
volatile uint8_t benchmark_event_transport = 0;

static uint32_t benchmark_last_cycles = 0;
static uint32_t benchmark_cycles_high = 0;

static uint32_t benchmark_cycles(void);
static void serial_send_benchmark_event(uint32_t sequence, uint64_t event_cycles);
static void hid_send_benchmark_event(uint32_t sequence, uint64_t event_cycles);

void benchmark_counter_init(void)
{
	static uint8_t initialized = 0;

	if (initialized) {
		return;
	}

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	DWT->CYCCNT = 0;
	initialized = 1;
}

static uint32_t benchmark_cycles(void)
{
	return DWT->CYCCNT;
}

uint64_t benchmark_cycles64(void)
{
	uint32_t now = benchmark_cycles();
	uint64_t full;

	taskENTER_CRITICAL();
	if (now < benchmark_last_cycles) {
		benchmark_cycles_high++;
	}
	benchmark_last_cycles = now;
	full = (((uint64_t) benchmark_cycles_high) << 32) | now;
	taskEXIT_CRITICAL();

	return full;
}

void benchmark_write_u32_le(uint8_t *dst, uint32_t value)
{
	dst[0] = (uint8_t)(value & 0xFF);
	dst[1] = (uint8_t)((value >> 8) & 0xFF);
	dst[2] = (uint8_t)((value >> 16) & 0xFF);
	dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

void benchmark_write_u64_le(uint8_t *dst, uint64_t value)
{
	for (uint8_t i = 0; i < 8; i++) {
		dst[i] = (uint8_t)((value >> (i * 8)) & 0xFF);
	}
}

uint32_t benchmark_read_u32_le(const uint8_t *src)
{
	return ((uint32_t) src[0]) |
		(((uint32_t) src[1]) << 8) |
		(((uint32_t) src[2]) << 16) |
		(((uint32_t) src[3]) << 24);
}

uint8_t benchmark_quiet_active(void)
{
	return ((int32_t)(benchmark_quiet_until_ms - HAL_GetTick()) > 0) ? 1 : 0;
}

static void serial_send_benchmark_event(uint32_t sequence, uint64_t event_cycles)
{
	uint8_t cmd_tmp[1 + 1 + 1 + BENCHMARK_EVENT_REPLY_PAYLOAD + 1];
	uint8_t idx = 0;
	uint64_t tx_cycles = benchmark_cycles64();

	cmd_tmp[idx++] = 0xFF;
	cmd_tmp[idx++] = SERIAL_CMD_BENCHMARK_EVENT;
	cmd_tmp[idx++] = BENCHMARK_EVENT_REPLY_PAYLOAD;
	benchmark_write_u32_le(&cmd_tmp[idx], sequence);
	idx += 4;
	benchmark_write_u64_le(&cmd_tmp[idx], event_cycles);
	idx += 8;
	benchmark_write_u64_le(&cmd_tmp[idx], tx_cycles);
	idx += 8;
	benchmark_write_u32_le(&cmd_tmp[idx], SystemCoreClock);
	idx += 4;
	cmd_tmp[idx] = 0;
	for (uint8_t i = 0; i < idx; i++) {
		cmd_tmp[idx] += cmd_tmp[i];
	}
	(void) usb_reporter_cdc_enqueue_high(cmd_tmp, idx + 1);
}

static void hid_send_benchmark_event(uint32_t sequence, uint64_t event_cycles)
{
	uint64_t tx_cycles = benchmark_cycles64();
	(void) mai2_hid_benchmark_send_report((uint16_t) sequence, event_cycles, tx_cycles, SystemCoreClock);
}

void benchmark_emit_pending_event(void)
{
	uint32_t sequence = 0;
	uint64_t event_cycles = 0;
	uint32_t now_ms = HAL_GetTick();

	if (!benchmark_event_pending || ((int32_t)(now_ms - benchmark_event_due_ms) < 0)) {
		return;
	}

	taskENTER_CRITICAL();
	if (!benchmark_event_pending || ((int32_t)(HAL_GetTick() - benchmark_event_due_ms) < 0)) {
		taskEXIT_CRITICAL();
		return;
	}
	benchmark_event_pending = 0;
	sequence = benchmark_event_sequence;
	event_cycles = benchmark_cycles64();
	uint8_t transport = benchmark_event_transport;
	taskEXIT_CRITICAL();

	if (transport == 2) {
		hid_send_benchmark_event(sequence, event_cycles);
	} else {
		serial_send_benchmark_event(sequence, event_cycles);
	}
}
