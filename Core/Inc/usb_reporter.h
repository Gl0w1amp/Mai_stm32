/*
 * usb_reporter.h
 *
 * Central USB report scheduler for CDC and HID endpoints.
 */

#ifndef INC_USB_REPORTER_H_
#define INC_USB_REPORTER_H_

#include <stdint.h>

typedef struct {
	uint32_t high_enqueued_count;
	uint32_t low_enqueued_count;
	uint32_t high_drop_count;
	uint32_t low_drop_count;
	uint32_t tx_ok_count;
	uint32_t tx_busy_retry_count;
	uint32_t tx_fail_retry_count;
	uint32_t tx_giveup_count;
	uint32_t last_tx_latency_ms;
	uint32_t max_tx_latency_ms;
	uint32_t last_retry_count;
	uint32_t max_retry_count;
} usb_cdc_tx_stats_t;

typedef struct {
	uint32_t frame_start_count;
	uint32_t part_send_ok_count;
	uint32_t busy_retry_count;
	uint32_t not_ready_retry_count;
	uint32_t send_fail_count;
	uint32_t stale_drop_count;
	uint32_t last_frame_interval_ms;
	uint32_t max_frame_interval_ms;
	uint32_t last_part_latency_ms;
	uint32_t max_part_latency_ms;
	uint32_t dropped_frames;
	uint8_t pending;
	uint8_t part_index;
	uint8_t in_ready;
	uint8_t reserved;
} usb_touch_hid_stats_t;

uint8_t usb_reporter_init(void);
void usb_reporter_reset(void);
void usb_reporter_service(uint8_t debug_flag, uint8_t debug_stream_mode,
		uint8_t heartbeat_active, uint8_t touch_scan_enabled,
		uint8_t benchmark_quiet);

uint8_t usb_reporter_cdc_enqueue_high(const uint8_t *buf, uint16_t len);
uint8_t usb_reporter_cdc_enqueue_high_isr(const uint8_t *buf, uint16_t len);
uint8_t usb_reporter_cdc_enqueue_low(const uint8_t *buf, uint16_t len);
uint32_t usb_reporter_cdc_low_spaces_available(void);
void usb_reporter_cdc_stats_reset(void);
void usb_reporter_cdc_stats_snapshot(usb_cdc_tx_stats_t *stats_out,
		uint32_t *high_depth_out, uint32_t *low_depth_out);
void usb_reporter_touch_hid_stats_reset(void);
void usb_reporter_touch_hid_stats_snapshot(usb_touch_hid_stats_t *stats_out);

uint8_t usb_reporter_custom_hid_enqueue(const uint8_t *report, uint16_t len);

void usb_reporter_notify_cdc_in_complete(uint8_t cdc_ch);
void usb_reporter_notify_custom_hid_in_complete(void);
void usb_reporter_notify_keyboard_hid_in_complete(void);
void usb_reporter_notify_touch_hid_in_complete(void);

#endif /* INC_USB_REPORTER_H_ */
