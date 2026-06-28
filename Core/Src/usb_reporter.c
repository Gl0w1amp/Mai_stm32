/*
 * usb_reporter.c
 *
 * Centralized USB report scheduling.
 */

#include "usb_reporter.h"

#include "FreeRTOS.h"
#include "capsense_sim.h"
#include "input_snapshot.h"
#include "queue.h"
#include "serial_reports.h"
#include "byte_pack.h"
#include "slider.h"
#include "task.h"
#include "usbd_cdc_acm_if.h"
#include "usbd_hid_custom_if.h"
#include "usbd_hid_keyboard.h"
#include "usbd_hid_touch_if.h"
#include "usbd_hid_vendor_if.h"
#include <string.h>

#define USB_REPORTER_CDC_HIGH_QUEUE_LENGTH 8u
#define USB_REPORTER_CDC_LOW_QUEUE_LENGTH 8u
#define USB_REPORTER_CDC_PACKET_SIZE 64u
#define USB_REPORTER_CUSTOM_QUEUE_LENGTH 8u
#define USB_REPORTER_CUSTOM_REPORT_SIZE 24u
#define USB_REPORTER_VENDOR_QUEUE_LENGTH 8u
#define USB_REPORTER_VENDOR_REPORT_SIZE 64u
#define USB_REPORTER_KEYBOARD_REPORT_SIZE 14u
#define USB_REPORTER_CDC_LIVE_INTERVAL_MS 2u
#define USB_REPORTER_CDC_RETRY_GIVEUP_MS 50u

#define USB_TOUCH_REPORT_INTERVAL_MS 5u
#define USB_TOUCH_REPORT_STALE_PENDING_MS 15u
#define USB_TOUCH_REPORT_SIZE 64u
#define USB_TOUCH_REPORT_VALUES_PER_PART 17u
#define USB_TOUCH_REPORT_PART_COUNT 2u
#define USB_TOUCH_REPORT_MODE_DELTA16_LOGICAL 0x01u
#define USB_TOUCH_REPORT_FLAG_DELTA 0x01u
#define USB_TOUCH_REPORT_FLAG_LOGICAL_ORDER 0x02u
#define USB_TOUCH_REPORT_FLAG_TOUCH_BITS_VALID 0x04u

typedef struct {
	uint16_t len;
	uint32_t enqueue_tick;
	uint8_t data[USB_REPORTER_CDC_PACKET_SIZE];
} usb_reporter_cdc_packet_t;

typedef struct {
	uint16_t len;
	uint8_t data[USB_REPORTER_CUSTOM_REPORT_SIZE];
} usb_reporter_custom_packet_t;

typedef struct {
	uint16_t len;
	uint8_t data[USB_REPORTER_VENDOR_REPORT_SIZE];
} usb_reporter_vendor_packet_t;

typedef struct {
	uint8_t pending;
	uint8_t live_packet;
	uint16_t len;
	uint32_t enqueue_tick;
	uint32_t retry_count;
	uint8_t data[USB_REPORTER_CDC_PACKET_SIZE];
} usb_reporter_cdc_endpoint_t;

typedef struct {
	uint8_t pending;
	uint16_t len;
	uint8_t data[USB_REPORTER_CUSTOM_REPORT_SIZE];
} usb_reporter_custom_endpoint_t;

typedef struct {
	uint8_t pending;
	uint16_t len;
	uint8_t data[USB_REPORTER_VENDOR_REPORT_SIZE];
} usb_reporter_vendor_endpoint_t;

typedef struct {
	input_snapshot_t active_snapshot;
	uint8_t active_stream_sequence;
	uint8_t frame_pending;
	uint8_t part_index;
	uint32_t last_frame_start_tick;
	uint32_t pending_frame_start_tick;
	uint16_t dropped_frames;
	uint8_t report_buffer[USB_TOUCH_REPORT_SIZE];
} usb_reporter_touch_endpoint_t;

extern USBD_HandleTypeDef hUsbDevice;
extern uint8_t keyboard_sheet[14];

static QueueHandle_t cdc_high_queue = NULL;
static QueueHandle_t cdc_low_queue = NULL;
static QueueHandle_t custom_hid_queue = NULL;
static QueueHandle_t vendor_hid_queue = NULL;
static usb_cdc_tx_stats_t cdc_stats = {0};
static usb_touch_hid_stats_t touch_hid_stats = {0};
static usb_reporter_cdc_endpoint_t cdc_ep = {0};
static usb_reporter_cdc_packet_t cdc_live_latest = {0};
static uint8_t cdc_live_latest_pending = 0u;
static usb_reporter_custom_endpoint_t custom_ep = {0};
static usb_reporter_vendor_endpoint_t vendor_ep = {0};
static usb_reporter_touch_endpoint_t touch_ep = {0};
static uint8_t last_keyboard_report[USB_REPORTER_KEYBOARD_REPORT_SIZE] = {0};
static uint8_t last_custom_buttons0 = 0xFFu;
static uint8_t last_custom_io_status = 0xFFu;
static uint8_t cdc_in_ready = 1u;
static uint8_t custom_hid_in_ready = 1u;
static uint8_t vendor_hid_in_ready = 1u;
static uint8_t keyboard_hid_in_ready = 1u;
static uint8_t touch_hid_in_ready = 1u;
static uint32_t cdc_in_flight_start_tick = 0u;
static uint32_t cdc_live_last_enqueue_tick = 0u;

static uint8_t usb_reporter_cdc_enqueue(QueueHandle_t queue,
		const uint8_t *buf, uint16_t len)
{
	usb_reporter_cdc_packet_t packet;
	BaseType_t result;

	if ((queue == NULL) || (buf == NULL) || (len == 0u) ||
			(len > sizeof(packet.data))) {
		return 0u;
	}

	packet.len = len;
	packet.enqueue_tick = HAL_GetTick();
	memcpy(packet.data, buf, len);

	result = xQueueSend(queue, &packet, 0);
	if (queue == cdc_high_queue) {
		if (result == pdPASS) {
			cdc_stats.high_enqueued_count++;
		} else {
			cdc_stats.high_drop_count++;
		}
	} else if (queue == cdc_low_queue) {
		if (result == pdPASS) {
			cdc_stats.low_enqueued_count++;
		} else {
			cdc_stats.low_drop_count++;
		}
	}

	return (uint8_t)(result == pdPASS);
}

static uint8_t usb_reporter_cdc_dequeue(usb_reporter_cdc_endpoint_t *ep)
{
	usb_reporter_cdc_packet_t packet;

	if (ep == NULL) {
		return 0u;
	}

	if ((cdc_high_queue != NULL) &&
			(xQueueReceive(cdc_high_queue, &packet, 0) == pdPASS)) {
		/* high priority wins */
	} else if ((cdc_low_queue != NULL) &&
			(xQueueReceive(cdc_low_queue, &packet, 0) == pdPASS)) {
		/* low priority follows */
	} else {
		return 0u;
	}

	ep->pending = 1u;
	ep->live_packet = 0u;
	ep->len = packet.len;
	ep->enqueue_tick = packet.enqueue_tick;
	ep->retry_count = 0u;
	memcpy(ep->data, packet.data, packet.len);
	return 1u;
}

static uint8_t usb_reporter_cdc_dequeue_live_latest(
		usb_reporter_cdc_endpoint_t *ep)
{
	if ((ep == NULL) || (cdc_live_latest_pending == 0u)) {
		return 0u;
	}

	ep->pending = 1u;
	ep->live_packet = 1u;
	ep->len = cdc_live_latest.len;
	ep->enqueue_tick = cdc_live_latest.enqueue_tick;
	ep->retry_count = 0u;
	memcpy(ep->data, cdc_live_latest.data, cdc_live_latest.len);
	cdc_live_latest_pending = 0u;
	return 1u;
}

static uint8_t usb_reporter_cdc_high_waiting(void)
{
	return ((cdc_high_queue != NULL) &&
			(uxQueueMessagesWaiting(cdc_high_queue) > 0u)) ? 1u : 0u;
}

static uint8_t usb_reporter_cdc_update_live_latest(const uint8_t *buf,
		uint16_t len)
{
	uint32_t now;

	if ((buf == NULL) || (len == 0u) ||
			(len > sizeof(cdc_live_latest.data))) {
		return 0u;
	}

	now = HAL_GetTick();
	cdc_stats.low_enqueued_count++;

	if ((cdc_ep.pending != 0u) && (cdc_ep.live_packet != 0u)) {
		cdc_stats.low_drop_count++;
		cdc_ep.len = len;
		cdc_ep.enqueue_tick = now;
		cdc_ep.retry_count = 0u;
		memcpy(cdc_ep.data, buf, len);
		return 1u;
	}

	if (cdc_live_latest_pending != 0u) {
		cdc_stats.low_drop_count++;
	}
	cdc_live_latest.len = len;
	cdc_live_latest.enqueue_tick = now;
	memcpy(cdc_live_latest.data, buf, len);
	cdc_live_latest_pending = 1u;
	return 1u;
}

static void usb_reporter_cdc_record_giveup(uint32_t now)
{
	uint32_t tx_latency_ms = now - cdc_ep.enqueue_tick;

	cdc_stats.tx_giveup_count++;
	cdc_stats.last_tx_latency_ms = tx_latency_ms;
	cdc_stats.last_retry_count = cdc_ep.retry_count;
	if (tx_latency_ms > cdc_stats.max_tx_latency_ms) {
		cdc_stats.max_tx_latency_ms = tx_latency_ms;
	}
	if (cdc_ep.retry_count > cdc_stats.max_retry_count) {
		cdc_stats.max_retry_count = cdc_ep.retry_count;
	}
}

static void usb_reporter_cdc_recover_in_stall(uint32_t now)
{
	extern USBD_CDC_ACM_HandleTypeDef CDC_ACM_Class_Data[];
	uint32_t tx_latency_ms = now - cdc_in_flight_start_tick;

	cdc_stats.tx_giveup_count++;
	cdc_stats.last_tx_latency_ms = tx_latency_ms;
	cdc_stats.last_retry_count = cdc_ep.retry_count;
	if (tx_latency_ms > cdc_stats.max_tx_latency_ms) {
		cdc_stats.max_tx_latency_ms = tx_latency_ms;
	}
	if (cdc_ep.retry_count > cdc_stats.max_retry_count) {
		cdc_stats.max_retry_count = cdc_ep.retry_count;
	}
	CDC_ACM_Class_Data[0].TxState = 0U;
	if (hUsbDevice.dev_state == USBD_STATE_CONFIGURED) {
		(void)USBD_LL_FlushEP(&hUsbDevice, CDC_IN_EP[0]);
	}
	cdc_in_ready = 1u;
	cdc_in_flight_start_tick = 0u;
	cdc_ep.retry_count = 0u;
}

static void usb_reporter_cdc_service_endpoint(void)
{
	uint8_t tx_result;
	uint32_t now;
	uint32_t tx_latency_ms;

	now = HAL_GetTick();
	if (((cdc_in_ready == 0u) || (CDC_TransmitReady(0u) == 0u)) &&
			(cdc_in_flight_start_tick != 0u) &&
			((uint32_t)(now - cdc_in_flight_start_tick) >=
			USB_REPORTER_CDC_RETRY_GIVEUP_MS)) {
		usb_reporter_cdc_recover_in_stall(now);
	}

	if ((cdc_ep.pending != 0u) && (cdc_ep.live_packet != 0u) &&
			(usb_reporter_cdc_high_waiting() != 0u)) {
		cdc_stats.low_drop_count++;
		memset(&cdc_ep, 0, sizeof(cdc_ep));
	}
	if (cdc_ep.pending == 0u) {
		(void)usb_reporter_cdc_dequeue(&cdc_ep);
	}
	if (cdc_ep.pending == 0u) {
		(void)usb_reporter_cdc_dequeue_live_latest(&cdc_ep);
	}
	if (cdc_ep.pending == 0u) {
		return;
	}
	if ((cdc_in_ready == 0u) || (CDC_TransmitReady(0u) == 0u)) {
		cdc_stats.tx_busy_retry_count++;
		cdc_ep.retry_count++;
		if ((cdc_in_flight_start_tick != 0u) &&
				((uint32_t)(now - cdc_in_flight_start_tick) >=
				USB_REPORTER_CDC_RETRY_GIVEUP_MS)) {
			usb_reporter_cdc_recover_in_stall(now);
		}
		return;
	}

	tx_result = CDC_Transmit(0u, cdc_ep.data, cdc_ep.len);
	if (tx_result == (uint8_t)USBD_OK) {
		now = HAL_GetTick();
		tx_latency_ms = now - cdc_ep.enqueue_tick;
		cdc_in_ready = 0u;
		cdc_in_flight_start_tick = now;
		cdc_stats.tx_ok_count++;
		cdc_stats.last_tx_latency_ms = tx_latency_ms;
		cdc_stats.last_retry_count = cdc_ep.retry_count;
		if (tx_latency_ms > cdc_stats.max_tx_latency_ms) {
			cdc_stats.max_tx_latency_ms = tx_latency_ms;
		}
		if (cdc_ep.retry_count > cdc_stats.max_retry_count) {
			cdc_stats.max_retry_count = cdc_ep.retry_count;
		}
		memset(&cdc_ep, 0, sizeof(cdc_ep));
		return;
	}

	cdc_ep.retry_count++;
	if (tx_result == (uint8_t)USBD_BUSY) {
		cdc_stats.tx_busy_retry_count++;
	} else {
		cdc_stats.tx_fail_retry_count++;
	}

	now = HAL_GetTick();
	if ((uint32_t)(now - cdc_ep.enqueue_tick) >=
			USB_REPORTER_CDC_RETRY_GIVEUP_MS) {
		usb_reporter_cdc_record_giveup(now);
		memset(&cdc_ep, 0, sizeof(cdc_ep));
	}
}

static void usb_reporter_maybe_enqueue_cdc_live(uint8_t debug_flag,
		uint8_t debug_stream_mode, uint8_t heartbeat_active,
		uint8_t touch_scan_enabled, uint8_t benchmark_quiet)
{
	input_snapshot_t snapshot;
	uint8_t report[14] = {0};
	uint8_t report_len = 0u;
	uint32_t now;

	if ((debug_flag != 0u) || (benchmark_quiet != 0u)) {
		return;
	}
	if ((heartbeat_active == 0u) &&
			((touch_scan_enabled == 0u) ||
			(debug_stream_mode == SERIAL_DEBUG_STREAM_MODE_RAW_34))) {
		return;
	}
	now = HAL_GetTick();
	if ((uint32_t)(now - cdc_live_last_enqueue_tick) <
			USB_REPORTER_CDC_LIVE_INTERVAL_MS) {
		return;
	}
	if (input_snapshot_get_latest(&snapshot) == 0u) {
		return;
	}

	if (heartbeat_active != 0u) {
		if (serial_reports_build_live_state_frame(SERIAL_CMD_AUTO_SCAN,
				&snapshot, report, &report_len) != 0u) {
			if (usb_reporter_cdc_update_live_latest(report, report_len) != 0u) {
				cdc_live_last_enqueue_tick = now;
			}
		}
	} else if ((touch_scan_enabled != 0u) &&
			(debug_stream_mode != SERIAL_DEBUG_STREAM_MODE_RAW_34)) {
		uint8_t touch_report[9] = {0};
		if (serial_reports_build_touch_scan_frame(&snapshot,
				touch_report, &report_len) != 0u) {
			if (usb_reporter_cdc_update_live_latest(touch_report,
					report_len) != 0u) {
				cdc_live_last_enqueue_tick = now;
			}
		}
	}
}

static void usb_reporter_custom_service_endpoint(void)
{
	usb_reporter_custom_packet_t packet;
	uint8_t status;

	if (custom_ep.pending == 0u) {
		if ((custom_hid_queue == NULL) ||
				(xQueueReceive(custom_hid_queue, &packet, 0) != pdPASS)) {
			return;
		}
		custom_ep.pending = 1u;
		custom_ep.len = packet.len;
		memcpy(custom_ep.data, packet.data, packet.len);
	}
	if ((custom_hid_in_ready == 0u) || (mai2_hid_custom_ready() == 0u)) {
		return;
	}

	status = mai2_hid_custom_send_report(custom_ep.data, custom_ep.len);
	if (status == (uint8_t)USBD_OK) {
		custom_hid_in_ready = 0u;
		memset(&custom_ep, 0, sizeof(custom_ep));
	}
}

static void usb_reporter_vendor_service_endpoint(void)
{
	usb_reporter_vendor_packet_t packet;
	uint8_t status;

	if (vendor_ep.pending == 0u) {
		if ((vendor_hid_queue == NULL) ||
				(xQueueReceive(vendor_hid_queue, &packet, 0) != pdPASS)) {
			return;
		}
		vendor_ep.pending = 1u;
		vendor_ep.len = packet.len;
		memcpy(vendor_ep.data, packet.data, packet.len);
	}
	if ((vendor_hid_in_ready == 0u) || (mai2_hid_vendor_ready() == 0u)) {
		return;
	}

	status = mai2_hid_vendor_send_report(vendor_ep.data, vendor_ep.len);
	if (status == (uint8_t)USBD_OK) {
		vendor_hid_in_ready = 0u;
		memset(&vendor_ep, 0, sizeof(vendor_ep));
	}
}

static void usb_reporter_maybe_enqueue_custom_buttons(uint8_t debug_flag,
		uint8_t debug_stream_mode)
{
	static uint16_t sequence = 0u;
	input_snapshot_t snapshot;
	uint8_t report[USB_REPORTER_CUSTOM_REPORT_SIZE] = {0};

	if ((debug_flag != 0u) &&
			(debug_stream_mode == SERIAL_DEBUG_STREAM_MODE_RAW_34)) {
		return;
	}
	if (input_snapshot_get_latest(&snapshot) == 0u) {
		return;
	}
	if ((snapshot.button_bits[0] == last_custom_buttons0) &&
			(snapshot.button_bits[1] == last_custom_io_status)) {
		return;
	}

	report[0] = snapshot.button_bits[0];
	report[1] = snapshot.button_bits[1];
	put_u16le(&report[2], sequence);
	if (usb_reporter_custom_hid_enqueue(report, sizeof(report)) != 0u) {
		sequence++;
		last_custom_buttons0 = snapshot.button_bits[0];
		last_custom_io_status = snapshot.button_bits[1];
	}
}

static void usb_reporter_build_keyboard_report(const input_snapshot_t *snapshot,
		uint8_t heartbeat_active, uint8_t *report)
{
	memset(report, 0, USB_REPORTER_KEYBOARD_REPORT_SIZE);
	if ((snapshot == NULL) || (report == NULL)) {
		return;
	}

	if (heartbeat_active == 0u) {
		for (uint8_t i = 0u; i < 8u; i++) {
			report[i] = ((snapshot->button_bits[0] & (uint8_t)(1u << i)) != 0u) ?
					keyboard_sheet[i] : 0u;
		}
		for (uint8_t i = 0u; i < 6u; i++) {
			report[i + 8u] = ((snapshot->button_bits[1] & (uint8_t)(1u << i)) != 0u) ?
					keyboard_sheet[i + 8u] : 0u;
		}
	} else {
		report[11] = ((snapshot->button_bits[1] & (uint8_t)(1u << 3u)) != 0u) ?
				keyboard_sheet[11] : 0u;
	}
}

static void usb_reporter_keyboard_service(uint8_t heartbeat_active)
{
	input_snapshot_t snapshot;
	uint8_t report[USB_REPORTER_KEYBOARD_REPORT_SIZE] = {0};
	uint8_t status;

	if (capsense_sim_is_enabled() != 0u) {
		memset(last_keyboard_report, 0, sizeof(last_keyboard_report));
		keyboard_hid_in_ready = 1u;
		return;
	}

	if (input_snapshot_get_latest(&snapshot) == 0u) {
		return;
	}

	usb_reporter_build_keyboard_report(&snapshot, heartbeat_active, report);
	if (memcmp(last_keyboard_report, report, sizeof(report)) == 0) {
		return;
	}
	if ((keyboard_hid_in_ready == 0u) ||
			(USBD_HID_Keyboard_IsReady(&hUsbDevice) == 0u)) {
		return;
	}
	if (UsbTxGuard_Take(0u) == 0u) {
		return;
	}
	status = USBD_HID_Keybaord_SendReport(&hUsbDevice, report, sizeof(report));
	UsbTxGuard_Give();
	if (status == (uint8_t)USBD_OK) {
		keyboard_hid_in_ready = 0u;
		memcpy(last_keyboard_report, report, sizeof(report));
	}
}

static void usb_reporter_touch_drop_pending(void)
{
	touch_ep.frame_pending = 0u;
	touch_ep.part_index = 0u;
	touch_ep.dropped_frames++;
	touch_hid_stats.dropped_frames = touch_ep.dropped_frames;
}

static void usb_reporter_touch_build_part(void)
{
	uint8_t first_logical = (uint8_t)(touch_ep.part_index *
			USB_TOUCH_REPORT_VALUES_PER_PART);
	uint8_t value_count = USB_TOUCH_REPORT_VALUES_PER_PART;

	if ((uint8_t)(first_logical + value_count) > INPUT_SNAPSHOT_TOUCH_CHANNEL_COUNT) {
		value_count = (uint8_t)(INPUT_SNAPSHOT_TOUCH_CHANNEL_COUNT - first_logical);
	}

	memset(touch_ep.report_buffer, 0, sizeof(touch_ep.report_buffer));
	touch_ep.report_buffer[0] = TOUCH_HID_REPORT_ID;
	touch_ep.report_buffer[1] = 1u;
	touch_ep.report_buffer[2] = USB_TOUCH_REPORT_MODE_DELTA16_LOGICAL;
	touch_ep.report_buffer[3] = touch_ep.active_stream_sequence;
	touch_ep.report_buffer[4] = touch_ep.part_index;
	touch_ep.report_buffer[5] = USB_TOUCH_REPORT_PART_COUNT;
	touch_ep.report_buffer[6] = first_logical;
	touch_ep.report_buffer[7] = value_count;
	put_u32le(&touch_ep.report_buffer[8], touch_ep.active_snapshot.tick_ms);
	touch_ep.report_buffer[12] = USB_TOUCH_REPORT_FLAG_DELTA |
			USB_TOUCH_REPORT_FLAG_LOGICAL_ORDER |
			USB_TOUCH_REPORT_FLAG_TOUCH_BITS_VALID;
	touch_ep.report_buffer[13] = touch_ep.active_snapshot.link.flags;
	touch_ep.report_buffer[14] = touch_ep.active_snapshot.link.protocol_version;

	for (uint8_t i = 0u; i < value_count; i++) {
		uint16_t value = touch_ep.active_snapshot.touch_strength[first_logical + i];
		put_u16le(&touch_ep.report_buffer[16u + (i * 2u)], value);
	}

	memcpy(&touch_ep.report_buffer[50], touch_ep.active_snapshot.touch_bits,
			INPUT_SNAPSHOT_TOUCH_BITS_SIZE);
	put_u16le(&touch_ep.report_buffer[55], (uint16_t)touch_ep.dropped_frames);
	put_u32le(&touch_ep.report_buffer[57], touch_ep.active_snapshot.seq);
	put_u16le(&touch_ep.report_buffer[61], (uint16_t)touch_hid_stats.last_frame_interval_ms);
}

static uint8_t usb_reporter_touch_try_send_part(void)
{
	uint8_t status;
	uint32_t part_latency_ms;

	if (touch_ep.frame_pending == 0u) {
		return 0u;
	}
	if ((touch_hid_in_ready == 0u) || (mai2_hid_touch_ready() == 0u)) {
		touch_hid_stats.not_ready_retry_count++;
		return 0u;
	}

	usb_reporter_touch_build_part();
	status = mai2_hid_touch_send_report(touch_ep.report_buffer,
			sizeof(touch_ep.report_buffer));
	if (status == (uint8_t)USBD_OK) {
		part_latency_ms = HAL_GetTick() - touch_ep.pending_frame_start_tick;
		touch_hid_stats.part_send_ok_count++;
		touch_hid_stats.last_part_latency_ms = part_latency_ms;
		if (part_latency_ms > touch_hid_stats.max_part_latency_ms) {
			touch_hid_stats.max_part_latency_ms = part_latency_ms;
		}
		touch_hid_in_ready = 0u;
		touch_ep.part_index++;
		if (touch_ep.part_index >= USB_TOUCH_REPORT_PART_COUNT) {
			touch_ep.frame_pending = 0u;
			touch_ep.part_index = 0u;
			touch_ep.active_stream_sequence++;
		}
		return 1u;
	}

	if (status == (uint8_t)USBD_BUSY) {
		touch_hid_stats.busy_retry_count++;
		return 0u;
	}

	touch_hid_stats.send_fail_count++;
	usb_reporter_touch_drop_pending();

	return 0u;
}

static void usb_reporter_touch_service(void)
{
	uint32_t now = HAL_GetTick();

	if (touch_ep.frame_pending != 0u) {
		(void)usb_reporter_touch_try_send_part();
		if (touch_ep.frame_pending != 0u) {
			if ((uint32_t)(now - touch_ep.pending_frame_start_tick) >=
					USB_TOUCH_REPORT_STALE_PENDING_MS) {
				touch_hid_stats.stale_drop_count++;
				usb_reporter_touch_drop_pending();
			}
			return;
		}
	}

	if ((uint32_t)(now - touch_ep.last_frame_start_tick) <
			USB_TOUCH_REPORT_INTERVAL_MS) {
		return;
	}
	if (input_snapshot_get_latest(&touch_ep.active_snapshot) == 0u) {
		return;
	}

	if (touch_ep.last_frame_start_tick != 0u) {
		uint32_t interval = now - touch_ep.last_frame_start_tick;
		touch_hid_stats.last_frame_interval_ms = interval;
		if (interval > touch_hid_stats.max_frame_interval_ms) {
			touch_hid_stats.max_frame_interval_ms = interval;
		}
	}
	touch_hid_stats.frame_start_count++;
	touch_ep.last_frame_start_tick = now;
	touch_ep.pending_frame_start_tick = now;
	touch_ep.part_index = 0u;
	touch_ep.frame_pending = 1u;
	(void)usb_reporter_touch_try_send_part();
}

uint8_t usb_reporter_init(void)
{
	if (cdc_high_queue == NULL) {
		cdc_high_queue = xQueueCreate(USB_REPORTER_CDC_HIGH_QUEUE_LENGTH,
				sizeof(usb_reporter_cdc_packet_t));
	}
	if (cdc_low_queue == NULL) {
		cdc_low_queue = xQueueCreate(USB_REPORTER_CDC_LOW_QUEUE_LENGTH,
				sizeof(usb_reporter_cdc_packet_t));
	}
	if (custom_hid_queue == NULL) {
		custom_hid_queue = xQueueCreate(USB_REPORTER_CUSTOM_QUEUE_LENGTH,
				sizeof(usb_reporter_custom_packet_t));
	}
	if (vendor_hid_queue == NULL) {
		vendor_hid_queue = xQueueCreate(USB_REPORTER_VENDOR_QUEUE_LENGTH,
				sizeof(usb_reporter_vendor_packet_t));
	}

	return (uint8_t)((cdc_high_queue != NULL) &&
			(cdc_low_queue != NULL) &&
			(custom_hid_queue != NULL) &&
			(vendor_hid_queue != NULL));
}

void usb_reporter_reset(void)
{
	memset(&cdc_stats, 0, sizeof(cdc_stats));
	memset(&touch_hid_stats, 0, sizeof(touch_hid_stats));
	memset(&cdc_ep, 0, sizeof(cdc_ep));
	memset(&cdc_live_latest, 0, sizeof(cdc_live_latest));
	cdc_live_latest_pending = 0u;
	memset(&custom_ep, 0, sizeof(custom_ep));
	memset(&vendor_ep, 0, sizeof(vendor_ep));
	memset(&touch_ep, 0, sizeof(touch_ep));
	memset(last_keyboard_report, 0, sizeof(last_keyboard_report));
	last_custom_buttons0 = 0xFFu;
	last_custom_io_status = 0xFFu;
	cdc_in_ready = 1u;
	cdc_in_flight_start_tick = 0u;
	cdc_live_last_enqueue_tick = 0u;
	custom_hid_in_ready = 1u;
	vendor_hid_in_ready = 1u;
	keyboard_hid_in_ready = 1u;
	touch_hid_in_ready = 1u;
	if (cdc_high_queue != NULL) {
		(void)xQueueReset(cdc_high_queue);
	}
	if (cdc_low_queue != NULL) {
		(void)xQueueReset(cdc_low_queue);
	}
	if (custom_hid_queue != NULL) {
		(void)xQueueReset(custom_hid_queue);
	}
	if (vendor_hid_queue != NULL) {
		(void)xQueueReset(vendor_hid_queue);
	}
}

void usb_reporter_service(uint8_t debug_flag, uint8_t debug_stream_mode,
		uint8_t heartbeat_active, uint8_t touch_scan_enabled,
		uint8_t benchmark_quiet)
{
	usb_reporter_maybe_enqueue_cdc_live(debug_flag, debug_stream_mode,
			heartbeat_active, touch_scan_enabled, benchmark_quiet);
	usb_reporter_maybe_enqueue_custom_buttons(debug_flag, debug_stream_mode);
	usb_reporter_keyboard_service(heartbeat_active);
	usb_reporter_touch_service();
	usb_reporter_custom_service_endpoint();
	usb_reporter_vendor_service_endpoint();
	usb_reporter_cdc_service_endpoint();
}

uint8_t usb_reporter_cdc_enqueue_high(const uint8_t *buf, uint16_t len)
{
	return usb_reporter_cdc_enqueue(cdc_high_queue, buf, len);
}

uint8_t usb_reporter_cdc_enqueue_high_isr(const uint8_t *buf, uint16_t len)
{
	usb_reporter_cdc_packet_t packet;
	BaseType_t higher_priority_task_woken = pdFALSE;

	if ((cdc_high_queue == NULL) || (buf == NULL) || (len == 0u) ||
			(len > sizeof(packet.data))) {
		return 0u;
	}

	packet.len = len;
	packet.enqueue_tick = HAL_GetTick();
	memcpy(packet.data, buf, len);

	if (xQueueSendFromISR(cdc_high_queue, &packet,
			&higher_priority_task_woken) != pdPASS) {
		cdc_stats.high_drop_count++;
		return 0u;
	}

	cdc_stats.high_enqueued_count++;
	portYIELD_FROM_ISR(higher_priority_task_woken);
	return 1u;
}

uint8_t usb_reporter_cdc_enqueue_low(const uint8_t *buf, uint16_t len)
{
	return usb_reporter_cdc_enqueue(cdc_low_queue, buf, len);
}

uint32_t usb_reporter_cdc_low_spaces_available(void)
{
	return (cdc_low_queue != NULL) ?
			(uint32_t)uxQueueSpacesAvailable(cdc_low_queue) : 0u;
}

void usb_reporter_cdc_stats_reset(void)
{
	memset(&cdc_stats, 0, sizeof(cdc_stats));
}

void usb_reporter_cdc_stats_snapshot(usb_cdc_tx_stats_t *stats_out,
		uint32_t *high_depth_out, uint32_t *low_depth_out)
{
	if (stats_out != NULL) {
		*stats_out = cdc_stats;
	}
	if (high_depth_out != NULL) {
		*high_depth_out = (cdc_high_queue != NULL) ?
				(uint32_t)uxQueueMessagesWaiting(cdc_high_queue) : 0u;
	}
	if (low_depth_out != NULL) {
		*low_depth_out = (cdc_low_queue != NULL) ?
				(uint32_t)uxQueueMessagesWaiting(cdc_low_queue) : 0u;
		if (cdc_live_latest_pending != 0u) {
			(*low_depth_out)++;
		}
	}
}

void usb_reporter_touch_hid_stats_reset(void)
{
	memset(&touch_hid_stats, 0, sizeof(touch_hid_stats));
	touch_ep.dropped_frames = 0u;
}

void usb_reporter_touch_hid_stats_snapshot(usb_touch_hid_stats_t *stats_out)
{
	if (stats_out != NULL) {
		*stats_out = touch_hid_stats;
		stats_out->dropped_frames = touch_ep.dropped_frames;
		stats_out->pending = touch_ep.frame_pending;
		stats_out->part_index = touch_ep.part_index;
		stats_out->in_ready = touch_hid_in_ready;
	}
}

uint8_t usb_reporter_custom_hid_enqueue(const uint8_t *report, uint16_t len)
{
	usb_reporter_custom_packet_t packet;

	if ((custom_hid_queue == NULL) || (report == NULL) ||
			(len == 0u) || (len > sizeof(packet.data))) {
		return 0u;
	}

	packet.len = len;
	memset(packet.data, 0, sizeof(packet.data));
	memcpy(packet.data, report, len);
	return (uint8_t)(xQueueSend(custom_hid_queue, &packet, 0) == pdPASS);
}

uint8_t usb_reporter_vendor_hid_enqueue(const uint8_t *report, uint16_t len)
{
	usb_reporter_vendor_packet_t packet;

	if ((vendor_hid_queue == NULL) || (report == NULL) ||
			(len == 0u) || (len > sizeof(packet.data))) {
		return 0u;
	}

	packet.len = sizeof(packet.data);
	memset(packet.data, 0, sizeof(packet.data));
	memcpy(packet.data, report, len);
	return (uint8_t)(xQueueSend(vendor_hid_queue, &packet, 0) == pdPASS);
}

void usb_reporter_notify_cdc_in_complete(uint8_t cdc_ch)
{
	if (cdc_ch == 0u) {
		cdc_in_ready = 1u;
		cdc_in_flight_start_tick = 0u;
	}
}

void usb_reporter_notify_custom_hid_in_complete(void)
{
	custom_hid_in_ready = 1u;
}

void usb_reporter_notify_vendor_hid_in_complete(void)
{
	vendor_hid_in_ready = 1u;
}

void usb_reporter_notify_keyboard_hid_in_complete(void)
{
	keyboard_hid_in_ready = 1u;
}

void usb_reporter_notify_touch_hid_in_complete(void)
{
	touch_hid_in_ready = 1u;
}

uint8_t serial_cdc_tx_enqueue_high(const uint8_t *buf, uint16_t len)
{
	/* Keep command replies off CDC IN; CDC IN is reserved for legacy/live output. */
	return usb_reporter_vendor_hid_enqueue(buf, len);
}

uint8_t serial_cdc_tx_enqueue_high_isr(const uint8_t *buf, uint16_t len)
{
	return usb_reporter_cdc_enqueue_high_isr(buf, len);
}

uint8_t serial_cdc_tx_enqueue_low(const uint8_t *buf, uint16_t len)
{
	return usb_reporter_cdc_enqueue_low(buf, len);
}

uint32_t serial_cdc_tx_low_spaces_available(void)
{
	return usb_reporter_cdc_low_spaces_available();
}

void usb_cdc_tx_stats_reset(void)
{
	usb_reporter_cdc_stats_reset();
}

void usb_cdc_tx_stats_snapshot(usb_cdc_tx_stats_t *stats_out, uint32_t *high_depth_out, uint32_t *low_depth_out)
{
	usb_reporter_cdc_stats_snapshot(stats_out, high_depth_out, low_depth_out);
}
