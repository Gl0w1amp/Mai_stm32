/*
 * serial_commands.c
 *
 * Table-driven command dispatch for parsed CDC serial frames.
 */

#include "serial_commands.h"

#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "task.h"
#include "main.h"
#include "LED.h"
#include "capsense.h"
#include "dfu_jump.h"
#include "flash.h"
#include "debug_mode.h"
#include "app_state.h"
#include "input_snapshot.h"
#include "serial_reports.h"
#include "benchmark.h"
#include "serial_checksum.h"
#include "byte_pack.h"
#include "usbd_desc.h"
#include "usbd_hid_custom_if.h"
#include "usb_reporter.h"
#include <string.h>

#define BENCHMARK_EVENT_PAYLOAD 8u
#define BENCHMARK_EVENT_DELAY_MS_DEFAULT 10u
#define BENCHMARK_QUIET_PERIOD_MS 30u
#define TOUCH_CHANNEL_COUNT 34u
#define DELAY_SETTING_COUNT 2u
#define DELAY_SETTING_MAX 9u
#define HEART_BEAT_HOLD_MS 300u
#define DEBUG_EXIT_RESET_DELAY_MS 500u
#define DEBUG_EXIT_USB_DISCONNECT_HOLD_MS 500u

typedef struct {
	const uint8_t *data;
	uint8_t len;
	uint64_t dispatch_cycles;
} serial_command_context_t;

typedef void (*serial_command_handler_fn)(const serial_command_context_t *ctx);

typedef struct {
	uint8_t command;
	serial_command_handler_fn handler;
} serial_command_entry_t;

extern USBD_HandleTypeDef hUsbDevice;
extern const char VERSION[];
extern volatile uint8_t debug_exit_reset_pending;
extern volatile uint32_t debug_exit_reset_deadline_ms;
extern osThreadId CommandTaskHandle;
extern volatile uint32_t benchmark_quiet_until_ms;
extern volatile uint8_t benchmark_event_pending;
extern volatile uint32_t benchmark_event_due_ms;
extern volatile uint32_t benchmark_event_sequence;
extern volatile uint8_t benchmark_event_transport;
extern volatile uint8_t touch_scan_flag;

static volatile uint32_t heart_beat_deadline_ms = 0;

void heart_beat_refresh(void)
{
	heart_beat_deadline_ms = HAL_GetTick() + HEART_BEAT_HOLD_MS;
}

uint8_t heart_beat_active(void)
{
	return ((int32_t)(heart_beat_deadline_ms - HAL_GetTick()) > 0) ? 1u : 0u;
}

uint8_t controller_role_normalize(uint8_t role)
{
	return (role == 2u) ? 2u : 1u;
}

void debug_exit_reset_now(void)
{
	debug_exit_reset_pending = 0u;
	/* Keep USB offline long enough for Windows CDC to retire the current
	 * devnode before the MCU comes back and re-enumerates.
	 */
	(void) USBD_Stop(&hUsbDevice);
	(void) USBD_DeInit(&hUsbDevice);
	osDelay(DEBUG_EXIT_USB_DISCONNECT_HOLD_MS);
	NVIC_SystemReset();
}

void command_notify_ready_from_isr(void)
{
	BaseType_t higher_priority_task_woken = pdFALSE;

	if (CommandTaskHandle == NULL) {
		return;
	}

	vTaskNotifyGiveFromISR((TaskHandle_t) CommandTaskHandle, &higher_priority_task_woken);
	portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void handle_serial_cmd_led(const serial_command_context_t *ctx)
{
	/* Intentional no-op ack: legacy LED opcode is accepted but not acted on. */
	return;
}

static void handle_serial_cmd_led_button(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;

	if (rxBuffer[2] == 56u) {
		LED_update_button_rgb_fade(rxBuffer + 3, 8u);
		return;
	}

	if (rxBuffer[2] == 32u) {
		LED_update_button_rgb_speed(rxBuffer + 3, 8u);
		return;
	}

	uint8_t speed = 0u;
	if(rxBuffer[2] < 24u){
		return;
	}
	LED_SetButtonFrame(rxBuffer + 3);
	if (rxBuffer[2] >= 25u) {
		speed = rxBuffer[27];
	}
	LED_update_button(speed);
	return;
}

static void handle_serial_cmd_led_billboard(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 24){
		return;
	}
	/* Accepted no-op: the billboard frame buffer was write-only and
	 * never rendered, so the validated payload is intentionally
	 * dropped. */
	return;
}

static void handle_serial_cmd_led_pwm(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] < 3){
		return;
	}
	FET_LED_Update(rxBuffer[3],rxBuffer[4],rxBuffer[5]);
	return;
}

static void serial_send_led_status_response(uint8_t command, uint8_t ok)
{
	LED_Status status;
	uint8_t p[9] = {0};

	LED_StatusSnapshot(&status, HAL_GetTick());
	p[0] = ok;
	p[1] = status.mode;
	p[2] = status.host_active;
	p[3] = status.idle_effect;
	p[4] = status.idle_brightness;
	put_u16le(&p[5], status.host_timeout_ms);
	put_u16le(&p[7], status.host_remaining_ms);
	serial_reply_emit(command, p, 9u);
}

static void handle_serial_cmd_led_mode(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint8_t payload_len = rxBuffer[2];
	uint8_t ok = 1u;

	if (payload_len == 1u) {
		ok = LED_SetMode(rxBuffer[3], HAL_GetTick());
	} else if (payload_len != 0u) {
		ok = 0u;
	}

	serial_send_led_status_response(SERIAL_CMD_LED_MODE, ok);
}

static void handle_serial_cmd_led_config(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint8_t payload_len = rxBuffer[2];
	uint8_t ok = 1u;

	if (payload_len == 4u) {
		uint16_t timeout_ms = (uint16_t)rxBuffer[5] |
				(uint16_t)((uint16_t)rxBuffer[6] << 8);
		ok = LED_ConfigSet(rxBuffer[3], rxBuffer[4], timeout_ms);
	} else if (payload_len != 0u) {
		ok = 0u;
	}

	serial_send_led_status_response(SERIAL_CMD_LED_CONFIG, ok);
}

static void handle_serial_cmd_get_capsense_uart_stats(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	capsense_uart_stats_t stats;
	uint8_t cmd_tmp[40] = {0};
	uint8_t idx = 3;

	if ((rxBuffer[2] > 1) || ((rxBuffer[2] == 1) && (rxBuffer[3] != 1))) {
		return;
	}
	if ((rxBuffer[2] == 1) && (rxBuffer[3] == 1)) {
		capsense_uart_stats_reset();
	}

	capsense_uart_stats_get(&stats);

	cmd_tmp[0] = 0xff;
	cmd_tmp[1] = SERIAL_CMD_GET_CAPSENSE_UART_STATS;
	cmd_tmp[2] = 35;

	memcpy(&cmd_tmp[idx], &stats.checksum_accept_count, sizeof(stats.checksum_accept_count));
	idx += sizeof(stats.checksum_accept_count);
	memcpy(&cmd_tmp[idx], &stats.rolling_checksum_accept_count, sizeof(stats.rolling_checksum_accept_count));
	idx += sizeof(stats.rolling_checksum_accept_count);
	memcpy(&cmd_tmp[idx], &stats.legacy_accept_count, sizeof(stats.legacy_accept_count));
	idx += sizeof(stats.legacy_accept_count);
	memcpy(&cmd_tmp[idx], &stats.short_packet_count, sizeof(stats.short_packet_count));
	idx += sizeof(stats.short_packet_count);
	memcpy(&cmd_tmp[idx], &stats.empty_packet_count, sizeof(stats.empty_packet_count));
	idx += sizeof(stats.empty_packet_count);
	memcpy(&cmd_tmp[idx], &stats.parse_fail_count, sizeof(stats.parse_fail_count));
	idx += sizeof(stats.parse_fail_count);
	memcpy(&cmd_tmp[idx], &stats.uart_error_count, sizeof(stats.uart_error_count));
	idx += sizeof(stats.uart_error_count);
	memcpy(&cmd_tmp[idx], &stats.auto_reset_count, sizeof(stats.auto_reset_count));
	idx += sizeof(stats.auto_reset_count);
	cmd_tmp[idx++] = stats.protocol_version;
	cmd_tmp[idx++] = stats.legacy_payload_offset;
	cmd_tmp[idx++] = stats.rx_failure_streak;

	cmd_tmp[idx] = serial_checksum_sum(cmd_tmp, idx);
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, (uint16_t) (idx + 1));
	return;
}

static void handle_serial_cmd_get_usb_cdc_stats(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	usb_cdc_tx_stats_t stats;
	uint32_t high_depth = 0;
	uint32_t low_depth = 0;
	uint8_t cmd_tmp[64] = {0};
	uint8_t idx = 3;

	if ((rxBuffer[2] > 1) || ((rxBuffer[2] == 1) && (rxBuffer[3] != 1))) {
		return;
	}
	if ((rxBuffer[2] == 1) && (rxBuffer[3] == 1)) {
		usb_cdc_tx_stats_reset();
	}

	usb_cdc_tx_stats_snapshot(&stats, &high_depth, &low_depth);

	cmd_tmp[0] = 0xff;
	cmd_tmp[1] = SERIAL_CMD_GET_USB_CDC_STATS;
	cmd_tmp[2] = 56;

	memcpy(&cmd_tmp[idx], &stats.high_enqueued_count, sizeof(stats.high_enqueued_count));
	idx += sizeof(stats.high_enqueued_count);
	memcpy(&cmd_tmp[idx], &stats.low_enqueued_count, sizeof(stats.low_enqueued_count));
	idx += sizeof(stats.low_enqueued_count);
	memcpy(&cmd_tmp[idx], &stats.high_drop_count, sizeof(stats.high_drop_count));
	idx += sizeof(stats.high_drop_count);
	memcpy(&cmd_tmp[idx], &stats.low_drop_count, sizeof(stats.low_drop_count));
	idx += sizeof(stats.low_drop_count);
	memcpy(&cmd_tmp[idx], &stats.tx_ok_count, sizeof(stats.tx_ok_count));
	idx += sizeof(stats.tx_ok_count);
	memcpy(&cmd_tmp[idx], &stats.tx_busy_retry_count, sizeof(stats.tx_busy_retry_count));
	idx += sizeof(stats.tx_busy_retry_count);
	memcpy(&cmd_tmp[idx], &stats.tx_fail_retry_count, sizeof(stats.tx_fail_retry_count));
	idx += sizeof(stats.tx_fail_retry_count);
	memcpy(&cmd_tmp[idx], &stats.tx_giveup_count, sizeof(stats.tx_giveup_count));
	idx += sizeof(stats.tx_giveup_count);
	memcpy(&cmd_tmp[idx], &stats.last_tx_latency_ms, sizeof(stats.last_tx_latency_ms));
	idx += sizeof(stats.last_tx_latency_ms);
	memcpy(&cmd_tmp[idx], &stats.max_tx_latency_ms, sizeof(stats.max_tx_latency_ms));
	idx += sizeof(stats.max_tx_latency_ms);
	memcpy(&cmd_tmp[idx], &stats.last_retry_count, sizeof(stats.last_retry_count));
	idx += sizeof(stats.last_retry_count);
	memcpy(&cmd_tmp[idx], &stats.max_retry_count, sizeof(stats.max_retry_count));
	idx += sizeof(stats.max_retry_count);
	memcpy(&cmd_tmp[idx], &high_depth, sizeof(high_depth));
	idx += sizeof(high_depth);
	memcpy(&cmd_tmp[idx], &low_depth, sizeof(low_depth));
	idx += sizeof(low_depth);

	cmd_tmp[idx] = serial_checksum_sum(cmd_tmp, idx);
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, (uint16_t) (idx + 1));
}

static void handle_serial_cmd_get_touch_hid_stats(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	usb_touch_hid_stats_t stats;
	uint8_t cmd_tmp[64] = {0};
	uint8_t idx = 3;

	if ((rxBuffer[2] > 1) || ((rxBuffer[2] == 1) && (rxBuffer[3] != 1))) {
		return;
	}
	if ((rxBuffer[2] == 1) && (rxBuffer[3] == 1)) {
		usb_reporter_touch_hid_stats_reset();
	}

	usb_reporter_touch_hid_stats_snapshot(&stats);

	cmd_tmp[0] = 0xff;
	cmd_tmp[1] = SERIAL_CMD_GET_TOUCH_HID_STATS;
	cmd_tmp[2] = 48;

	memcpy(&cmd_tmp[idx], &stats.frame_start_count, sizeof(stats.frame_start_count));
	idx += sizeof(stats.frame_start_count);
	memcpy(&cmd_tmp[idx], &stats.part_send_ok_count, sizeof(stats.part_send_ok_count));
	idx += sizeof(stats.part_send_ok_count);
	memcpy(&cmd_tmp[idx], &stats.busy_retry_count, sizeof(stats.busy_retry_count));
	idx += sizeof(stats.busy_retry_count);
	memcpy(&cmd_tmp[idx], &stats.not_ready_retry_count, sizeof(stats.not_ready_retry_count));
	idx += sizeof(stats.not_ready_retry_count);
	memcpy(&cmd_tmp[idx], &stats.send_fail_count, sizeof(stats.send_fail_count));
	idx += sizeof(stats.send_fail_count);
	memcpy(&cmd_tmp[idx], &stats.stale_drop_count, sizeof(stats.stale_drop_count));
	idx += sizeof(stats.stale_drop_count);
	memcpy(&cmd_tmp[idx], &stats.last_frame_interval_ms, sizeof(stats.last_frame_interval_ms));
	idx += sizeof(stats.last_frame_interval_ms);
	memcpy(&cmd_tmp[idx], &stats.max_frame_interval_ms, sizeof(stats.max_frame_interval_ms));
	idx += sizeof(stats.max_frame_interval_ms);
	memcpy(&cmd_tmp[idx], &stats.last_part_latency_ms, sizeof(stats.last_part_latency_ms));
	idx += sizeof(stats.last_part_latency_ms);
	memcpy(&cmd_tmp[idx], &stats.max_part_latency_ms, sizeof(stats.max_part_latency_ms));
	idx += sizeof(stats.max_part_latency_ms);
	memcpy(&cmd_tmp[idx], &stats.dropped_frames, sizeof(stats.dropped_frames));
	idx += sizeof(stats.dropped_frames);
	cmd_tmp[idx++] = stats.pending;
	cmd_tmp[idx++] = stats.part_index;
	cmd_tmp[idx++] = stats.in_ready;
	cmd_tmp[idx++] = stats.reserved;

	cmd_tmp[idx] = serial_checksum_sum(cmd_tmp, idx);
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, (uint16_t) (idx + 1));
}

static void handle_serial_cmd_get_capsense_debug_stats(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	capsense_debug_stats_t stats;
	uint8_t cmd_tmp[32] = {0};
	uint8_t idx = 3;

	if ((rxBuffer[2] > 1) || ((rxBuffer[2] == 1) && (rxBuffer[3] != 1))) {
		return;
	}
	if ((rxBuffer[2] == 1) && (rxBuffer[3] == 1)) {
		capsense_debug_stats_reset();
	}

	capsense_debug_stats_get(&stats);

	cmd_tmp[0] = 0xff;
	cmd_tmp[1] = SERIAL_CMD_GET_CAPSENSE_DEBUG_STATS;
	cmd_tmp[2] = 20;

	memcpy(&cmd_tmp[idx], &stats.service_call_count, sizeof(stats.service_call_count));
	idx += sizeof(stats.service_call_count);
	memcpy(&cmd_tmp[idx], &stats.emit_batch_count, sizeof(stats.emit_batch_count));
	idx += sizeof(stats.emit_batch_count);
	memcpy(&cmd_tmp[idx], &stats.enqueue_attempt_count, sizeof(stats.enqueue_attempt_count));
	idx += sizeof(stats.enqueue_attempt_count);
	memcpy(&cmd_tmp[idx], &stats.enqueue_success_count, sizeof(stats.enqueue_success_count));
	idx += sizeof(stats.enqueue_success_count);
	cmd_tmp[idx++] = stats.last_queue_slots;
	cmd_tmp[idx++] = stats.last_debug_flag;
	cmd_tmp[idx++] = stats.last_debug_stream_mode;
	cmd_tmp[idx++] = stats.last_enqueue_success_mask;

	cmd_tmp[idx] = serial_checksum_sum(cmd_tmp, idx);
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, (uint16_t) (idx + 1));
}

static void handle_serial_cmd_get_controller_role(const serial_command_context_t *ctx)
{
	uint16_t pid = USBD_GetControllerPid();
	uint8_t p[3] = {
		controller_role_normalize(Flash.controller_role),
		(uint8_t) (pid & 0xFFu),
		(uint8_t) ((pid >> 8) & 0xFFu)
	};

	serial_reply_emit(SERIAL_CMD_GET_CONTROLLER_ROLE, p, 3u);
}

static void handle_serial_cmd_set_controller_role(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint8_t requested_role;
	uint8_t applied_role;
	uint8_t ok = 0;
	uint8_t p[2] = {0, 0};

	if(rxBuffer[2] != 1){
		return;
	}

	requested_role = rxBuffer[3];
	if ((requested_role == 1u) || (requested_role == 2u)) {
		applied_role = controller_role_normalize(requested_role);
		if (flash_set_controller_role(applied_role) != 0u) {
			player = applied_role;
			USBD_SetControllerRole(applied_role);
			ok = 1u;
			p[0] = applied_role;
			p[1] = ok;
		} else {
			p[0] = requested_role;
			p[1] = ok;
		}
	} else {
		p[0] = requested_role;
		p[1] = ok;
	}

	serial_reply_emit(SERIAL_CMD_SET_CONTROLLER_ROLE, p, 2u);

	if (ok != 0u) {
		osDelay(100);
		__disable_irq();
		NVIC_SystemReset();
	}
}

static void handle_serial_cmd_scan_start(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0){
		return;
	}
	/* Intentional no-op ack: opcode is accepted but scanning is always on. */
	return;
}

static void handle_serial_cmd_scan_stop(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0){
		return;
	}
	/* Intentional no-op ack: opcode is accepted but scanning is always on. */
	return;
}

static void handle_serial_cmd_read_mono_threshold(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 1){
		return;
	}
	if(rxBuffer[3] >= TOUCH_CHANNEL_COUNT){
		serial_send_simple_status(SERIAL_CMD_READ_MONO_THRESHOLD, 0u);
		return;
	}
	uint8_t p[3] = {0,0,0};
	p[0] = rxBuffer[3];
	memcpy(p + 1,&Flash.touch_threshold[p[0]],2);
	serial_reply_emit(SERIAL_CMD_READ_MONO_THRESHOLD, p, 3u);
}

static void handle_serial_cmd_write_mono_threshold(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 3){
		return;
	}
	if(rxBuffer[3] >= TOUCH_CHANNEL_COUNT){
		serial_send_simple_status(SERIAL_CMD_WRITE_MONO_THRESHOLD, 0u);
		return;
	}
	uint8_t index = rxBuffer[3];
	uint16_t value;
	memcpy(&value,&rxBuffer[4],2);
	uint8_t ok = flash_set_touch_threshold(index, value);
	serial_send_simple_status(SERIAL_CMD_WRITE_MONO_THRESHOLD, ok);
}

static void handle_serial_cmd_read_touch_sheet(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0){
		return;
	}
	uint8_t p[34] = {0};
	for(uint8_t i = 0;i<TOUCH_CHANNEL_COUNT;i++){
		p[i] = Flash.touch_sheet[i];
	}
	serial_reply_emit(SERIAL_CMD_READ_TOUCH_SHEET, p, 34u);
}

static void handle_serial_cmd_write_touch_sheet(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 34){
		return;
	}
	if (flash_touch_sheet_valid(&rxBuffer[3]) == 0u) {
		serial_send_simple_status(SERIAL_CMD_WRITE_TOUCH_SHEET, 0u);
		return;
	}
	uint8_t ok = flash_set_touch_sheet(&rxBuffer[3]);
	serial_send_simple_status(SERIAL_CMD_WRITE_TOUCH_SHEET, ok);
}

static void handle_serial_cmd_read_delay_setting(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 1){
		return;
	}else{
		if(rxBuffer[3] >= DELAY_SETTING_COUNT){
			serial_send_simple_status(SERIAL_CMD_READ_DELAY_SETTING, 0u);
			return;
		}
		uint8_t p[2];
		p[0] = rxBuffer[3];
		p[1] = Flash.delay_setting[rxBuffer[3]];
		serial_reply_emit(SERIAL_CMD_READ_DELAY_SETTING, p, 2u);
		return;
	}
}

static void handle_serial_cmd_write_delay_setting(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 2){
		return;
	}else{
		if((rxBuffer[3] >= DELAY_SETTING_COUNT) ||
			(rxBuffer[4] > DELAY_SETTING_MAX)){
			uint8_t p[2] = {rxBuffer[3],0};
			serial_reply_emit(SERIAL_CMD_WRITE_DELAY_SETTING, p, 2u);
			return;
		}
		uint8_t index = rxBuffer[3];
		if (flash_set_delay_setting(index, rxBuffer[4]) != 0u) {
			uint8_t p[1] = {index};
			serial_reply_emit(SERIAL_CMD_WRITE_DELAY_SETTING, p, 1u);
		} else {
			uint8_t p[2] = {index,0};
			serial_reply_emit(SERIAL_CMD_WRITE_DELAY_SETTING, p, 2u);
		}
		return;
	}
}

static void handle_serial_cmd_reset(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0){
		return;
	}
	// Send acknowledge before reset
	uint8_t one = 1u;
	serial_reply_emit(SERIAL_CMD_RESET, &one, 1u);
	// Wait for transmission to complete
	osDelay(100);
	// Perform software reset
	__disable_irq();
	NVIC_SystemReset();
}

static void handle_serial_cmd_jump_to_dfu(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0){
		return;
	}
	// Send acknowledge before jumping to DFU
	uint8_t one = 1u;
	serial_reply_emit(SERIAL_CMD_JUMP_TO_DFU, &one, 1u);
	// Wait for transmission to complete
	osDelay(200);  // Increased delay

	// Request DFU mode and reset (more reliable method)
	Request_DFU_Mode_And_Reset();
}

static void handle_serial_cmd_jump_to_bootloader(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0){
		return;
	}
	uint8_t one = 1u;
	serial_reply_emit(SERIAL_CMD_JUMP_TO_BOOTLOADER, &one, 1u);
	osDelay(200);
	Request_Affine_Bootloader_And_Reset();
}

static void handle_serial_cmd_benchmark(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint8_t rxLen = ctx->len;
	uint64_t dispatch_cycles = ctx->dispatch_cycles;
	uint16_t expected_len = (uint16_t) rxBuffer[2] + 4;
	if (rxBuffer[2] > BENCHMARK_MAX_PAYLOAD || rxLen < expected_len) {
		return;
	}
	benchmark_quiet_until_ms = HAL_GetTick() + BENCHMARK_QUIET_PERIOD_MS;
	serial_send_benchmark_reply(SERIAL_CMD_BENCHMARK, rxBuffer + 3, rxBuffer[2], dispatch_cycles);
}

static void handle_serial_cmd_benchmark_event(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint32_t delay_ms = BENCHMARK_EVENT_DELAY_MS_DEFAULT;
	if (rxBuffer[2] != BENCHMARK_EVENT_PAYLOAD) {
		return;
	}
	delay_ms = benchmark_read_u32_le(rxBuffer + 7);
	if (delay_ms == 0) {
		delay_ms = BENCHMARK_EVENT_DELAY_MS_DEFAULT;
	}
	taskENTER_CRITICAL();
	benchmark_event_sequence = benchmark_read_u32_le(rxBuffer + 3);
	benchmark_event_due_ms = HAL_GetTick() + delay_ms;
	benchmark_event_transport = 1;
	benchmark_event_pending = 1;
	taskEXIT_CRITICAL();
	benchmark_quiet_until_ms = HAL_GetTick() + delay_ms + BENCHMARK_QUIET_PERIOD_MS;
}

static void handle_serial_cmd_benchmark_hid_event(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint64_t dispatch_cycles = ctx->dispatch_cycles;
	uint32_t delay_ms = BENCHMARK_EVENT_DELAY_MS_DEFAULT;
	if (rxBuffer[2] != BENCHMARK_EVENT_PAYLOAD) {
		return;
	}
	delay_ms = benchmark_read_u32_le(rxBuffer + 7);
	if (delay_ms == 0) {
		delay_ms = BENCHMARK_EVENT_DELAY_MS_DEFAULT;
	}
	serial_send_benchmark_reply(SERIAL_CMD_BENCHMARK_HID_EVENT, rxBuffer + 3, rxBuffer[2], dispatch_cycles);
	taskENTER_CRITICAL();
	benchmark_event_sequence = benchmark_read_u32_le(rxBuffer + 3);
	benchmark_event_due_ms = HAL_GetTick() + delay_ms;
	benchmark_event_transport = 2;
	benchmark_event_pending = 1;
	taskEXIT_CRITICAL();
	benchmark_quiet_until_ms = HAL_GetTick() + delay_ms + BENCHMARK_QUIET_PERIOD_MS;
}

static void handle_serial_cmd_heart_beat(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0){
		return;
	}
	heart_beat_refresh();
	return;
}

static void handle_serial_cmd_to_debug_mode(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint8_t requested_mode = SERIAL_DEBUG_STREAM_MODE_FOCUS;
	uint8_t ok = 0u;

	if(rxBuffer[2] == 0u){
		ok = 1u;
	}else if((rxBuffer[2] == 1u) &&
		(rxBuffer[3] <= SERIAL_DEBUG_STREAM_MODE_RAW_34)){
		requested_mode = rxBuffer[3];
		ok = 1u;
	}

	if (ok != 0u) {
		mai2_hid_raw_debug_reset();
		debug_stream_mode = requested_mode;
		debug_flag = 1;
		(void)LED_SetMode(LED_MODE_DIAGNOSTIC, HAL_GetTick());
	}
	serial_reply_emit(SERIAL_CMD_TO_DEBUG_MODE, &ok, 1u);
	return;
}

static void handle_serial_cmd_set_debug_channel(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint8_t p[2] = {0, 0};
	if(rxBuffer[2] != 1){
		p[1] = 0;
	}else if(rxBuffer[3] < 34){
		debug_channel = rxBuffer[3];
		p[0] = debug_channel;
		p[1] = 1;
	}else{
		p[0] = rxBuffer[3];
		p[1] = 0;
	}
	serial_reply_emit(SERIAL_CMD_SET_DEBUG_CHANNEL, p, 2u);
}

static void handle_serial_cmd_exit_debug_mode(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0u){
		return;
	}

	/* Leaving debug mode should return the device to normal operation
	 * without forcing a USB disconnect/re-enumeration cycle.
	 */
	debug_exit_reset_pending = 0u;
	debug_flag = 0u;
	debug_stream_mode = SERIAL_DEBUG_STREAM_MODE_FOCUS;
	mai2_hid_raw_debug_reset();
	(void)LED_SetMode(LED_MODE_AUTO, HAL_GetTick());
	{
		uint8_t one = 1u;
		serial_reply_emit(SERIAL_CMD_EXIT_DEBUG_MODE, &one, 1u);
	}
}

static void handle_serial_cmd_get_live_state(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint8_t cmd_tmp[14] = {0};
	uint8_t cmd_len = 0u;
	input_snapshot_t snapshot;
	if(rxBuffer[2] != 0u){
		return;
	}
	if (input_snapshot_get_latest(&snapshot) == 0u) {
		return;
	}
	(void) serial_reports_build_live_state_frame(SERIAL_CMD_GET_LIVE_STATE,
		&snapshot, cmd_tmp, &cmd_len);
	if(cmd_len != 0u){
		(void) serial_cdc_tx_enqueue_high(cmd_tmp, cmd_len);
	}
}

static void handle_serial_cmd_get_raw_debug_snapshot(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	uint8_t sequence = 0u;
	uint8_t part_index = 0u;

	if ((rxBuffer[2] == 0u) ||
		(rxBuffer[2] > 2u) ||
		(debug_flag == 0u) ||
		(debug_stream_mode != SERIAL_DEBUG_STREAM_MODE_RAW_34)) {
		return;
	}

	sequence = rxBuffer[3];
	if (rxBuffer[2] == 2u) {
		part_index = rxBuffer[4];
	}

	(void) serial_send_raw_debug_snapshot(sequence, part_index);
}

static void handle_serial_cmd_get_board_info(const serial_command_context_t *ctx)
{
	const uint8_t *rxBuffer = ctx->data;
	if(rxBuffer[2] != 0){
		return;
	}
	char board_name[] = "1020-050201";
	uint8_t name_len = strlen(board_name);
	uint8_t version_len = strlen(VERSION);

	uint32_t uid[3];
	uid[0] = HAL_GetUIDw0();
	uid[1] = HAL_GetUIDw1();
	uid[2] = HAL_GetUIDw2();
	uint8_t uid_len = 12;

	uint8_t data_len = 1 + version_len + 1 + name_len + 1 + uid_len;
	uint8_t cmd_tmp[64];

	cmd_tmp[0] = 0xFF;
	cmd_tmp[1] = SERIAL_CMD_GET_BOARD_INFO;
	cmd_tmp[2] = data_len;

	uint8_t idx = 3;
	cmd_tmp[idx++] = version_len;
	memcpy(&cmd_tmp[idx], VERSION, version_len);
	idx += version_len;

	cmd_tmp[idx++] = name_len;
	memcpy(&cmd_tmp[idx], board_name, name_len);
	idx += name_len;

	cmd_tmp[idx++] = uid_len;
	memcpy(&cmd_tmp[idx], uid, uid_len);
	idx += uid_len;

	cmd_tmp[idx] = serial_checksum_sum(cmd_tmp, idx);
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, idx + 1);
}

static const serial_command_entry_t serial_command_table[] = {
	{ SERIAL_CMD_LED, handle_serial_cmd_led },
	{ SERIAL_CMD_LED_BUTTON, handle_serial_cmd_led_button },
	{ SERIAL_CMD_LED_BILLBOARD, handle_serial_cmd_led_billboard },
	{ SERIAL_CMD_LED_PWM, handle_serial_cmd_led_pwm },
	{ SERIAL_CMD_LED_MODE, handle_serial_cmd_led_mode },
	{ SERIAL_CMD_LED_CONFIG, handle_serial_cmd_led_config },
	{ SERIAL_CMD_GET_CAPSENSE_UART_STATS, handle_serial_cmd_get_capsense_uart_stats },
	{ SERIAL_CMD_GET_USB_CDC_STATS, handle_serial_cmd_get_usb_cdc_stats },
	{ SERIAL_CMD_GET_TOUCH_HID_STATS, handle_serial_cmd_get_touch_hid_stats },
	{ SERIAL_CMD_GET_CAPSENSE_DEBUG_STATS, handle_serial_cmd_get_capsense_debug_stats },
	{ SERIAL_CMD_GET_CONTROLLER_ROLE, handle_serial_cmd_get_controller_role },
	{ SERIAL_CMD_SET_CONTROLLER_ROLE, handle_serial_cmd_set_controller_role },
	{ SERIAL_CMD_SCAN_START, handle_serial_cmd_scan_start },
	{ SERIAL_CMD_SCAN_STOP, handle_serial_cmd_scan_stop },
	{ SERIAL_CMD_READ_MONO_THRESHOLD, handle_serial_cmd_read_mono_threshold },
	{ SERIAL_CMD_WRITE_MONO_THRESHOLD, handle_serial_cmd_write_mono_threshold },
	{ SERIAL_CMD_READ_TOUCH_SHEET, handle_serial_cmd_read_touch_sheet },
	{ SERIAL_CMD_WRITE_TOUCH_SHEET, handle_serial_cmd_write_touch_sheet },
	{ SERIAL_CMD_READ_DELAY_SETTING, handle_serial_cmd_read_delay_setting },
	{ SERIAL_CMD_WRITE_DELAY_SETTING, handle_serial_cmd_write_delay_setting },
	{ SERIAL_CMD_RESET, handle_serial_cmd_reset },
	{ SERIAL_CMD_JUMP_TO_DFU, handle_serial_cmd_jump_to_dfu },
	{ SERIAL_CMD_JUMP_TO_BOOTLOADER, handle_serial_cmd_jump_to_bootloader },
	{ SERIAL_CMD_BENCHMARK, handle_serial_cmd_benchmark },
	{ SERIAL_CMD_BENCHMARK_EVENT, handle_serial_cmd_benchmark_event },
	{ SERIAL_CMD_BENCHMARK_HID_EVENT, handle_serial_cmd_benchmark_hid_event },
	{ SERIAL_CMD_HEART_BEAT, handle_serial_cmd_heart_beat },
	{ SERIAL_CMD_TO_DEBUG_MODE, handle_serial_cmd_to_debug_mode },
	{ SERIAL_CMD_SET_DEBUG_CHANNEL, handle_serial_cmd_set_debug_channel },
	{ SERIAL_CMD_EXIT_DEBUG_MODE, handle_serial_cmd_exit_debug_mode },
	{ SERIAL_CMD_GET_LIVE_STATE, handle_serial_cmd_get_live_state },
	{ SERIAL_CMD_GET_RAW_DEBUG_SNAPSHOT, handle_serial_cmd_get_raw_debug_snapshot },
	{ SERIAL_CMD_GET_BOARD_INFO, handle_serial_cmd_get_board_info },
};

void serial_commands_process_frame(const serial_frame_t *frame,
		uint64_t dispatch_cycles)
{
	serial_command_context_t ctx;

	if ((frame == NULL) || (frame->len < 4u) ||
			(serial_protocol_frame_valid(frame->data, frame->len) == 0u)) {
		return;
	}

	ctx.data = frame->data;
	ctx.len = frame->len;
	ctx.dispatch_cycles = dispatch_cycles;

	for (uint8_t i = 0u; i < (uint8_t)(sizeof(serial_command_table) /
			sizeof(serial_command_table[0])); i++) {
		if (serial_command_table[i].command == frame->data[1]) {
			serial_command_table[i].handler(&ctx);
			return;
		}
	}
}

void serial_commands_process_legacy_ascii(const uint8_t *rxBuffer,
		uint8_t rxLen)
{
	char cmd_tmp[6] = "(RSET)";

	if ((rxBuffer == NULL) || (rxLen != 6u) || (rxBuffer[0] != 0x7Bu)) {
		return;
	}

	switch (rxBuffer[1]) {
	case 0x53:
		touch_scan_flag = 1u;
		break;
	case 0x48:
		touch_scan_flag = 0u;
		break;
	case 0x52:
		touch_scan_flag = 0u;
		if (rxBuffer[3] == 0x45u) {
			break;
		} else if (rxBuffer[3] == 0x72u) {
			player = 2u;
			memcpy(cmd_tmp + 1, rxBuffer + 1, 4);
		} else if (rxBuffer[3] == 0x6bu) {
			player = 2u;
			memcpy(cmd_tmp + 1, rxBuffer + 1, 4);
		}
		(void)serial_cdc_tx_enqueue_high((uint8_t *)cmd_tmp, 6u);
		break;
	case 0x4c:
		touch_scan_flag = 0u;
		player = 1u;
		if (rxBuffer[3] == 0x72u) {
			memcpy(cmd_tmp + 1, rxBuffer + 1, 4);
		} else if (rxBuffer[3] == 0x6bu) {
			memcpy(cmd_tmp + 1, rxBuffer + 1, 4);
		}
		(void)serial_cdc_tx_enqueue_high((uint8_t *)cmd_tmp, 6u);
		break;
	default:
		break;
	}
}
