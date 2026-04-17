/**
  ******************************************************************************
  * @file           : usbd_hid_custom_if.c
  * @brief          : USB custom HID interface.
  ******************************************************************************
  */

#include "usbd_hid_custom_if.h"
#include "usbd_cdc_acm_if.h"
#include <string.h>

#define MAI2_HID_REPORT_SIZE 24u
#define MAI2_HID_RAW_DEBUG_MAGIC0 0xA5u
#define MAI2_HID_RAW_DEBUG_MAGIC1 0x5Au
#define MAI2_HID_RAW_DEBUG_VALUES_PER_REPORT 9u
#define MAI2_HID_RAW_DEBUG_CHANNEL_COUNT 34u

static uint8_t raw_debug_sequence = 0u;
static uint8_t raw_debug_part_index = 0u;
static uint16_t raw_debug_snapshot[MAI2_HID_RAW_DEBUG_CHANNEL_COUNT] = {0};

__ALIGN_BEGIN static uint8_t CUSTOM_HID_ReportDesc[USBD_CUSTOM_HID_REPORT_DESC_SIZE] __ALIGN_END =
{
    0x06, 0xCA, 0xFF,
    0x09, 0x01,
    0xA1, 0x01,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x18,
    0x09, 0x01,
    0x81, 0x02,
    0xC0,
};

extern USBD_HandleTypeDef hUsbDevice;

static int8_t CUSTOM_HID_Init(void);
static int8_t CUSTOM_HID_DeInit(void);
static int8_t CUSTOM_HID_OutEvent(uint8_t event_idx, uint8_t state);

USBD_CUSTOM_HID_ItfTypeDef USBD_CustomHID_fops = {
    CUSTOM_HID_ReportDesc,
    CUSTOM_HID_Init,
    CUSTOM_HID_DeInit,
    CUSTOM_HID_OutEvent,
};

static int8_t CUSTOM_HID_Init(void)
{
  return (int8_t)USBD_OK;
}

static int8_t CUSTOM_HID_DeInit(void)
{
  return (int8_t)USBD_OK;
}

static int8_t CUSTOM_HID_OutEvent(uint8_t event_idx, uint8_t state)
{
  UNUSED(event_idx);
  UNUSED(state);
  return (int8_t)USBD_OK;
}

uint8_t mai2_hid_buttons_send_report(uint8_t buttons0, uint8_t io_status)
{
  static uint16_t sequence = 0;
  uint8_t report[MAI2_HID_REPORT_SIZE] = {0};
  uint8_t status;

  report[0] = buttons0;
  report[1] = io_status;
  report[2] = (uint8_t)(sequence & 0xFF);
  report[3] = (uint8_t)((sequence >> 8) & 0xFF);
  sequence++;

  if (UsbTxGuard_Take(0u) == 0u) {
    return (uint8_t)USBD_BUSY;
  }

  status = USBD_CUSTOM_HID_SendReport(&hUsbDevice, report, sizeof(report));
  UsbTxGuard_Give();
  return status;
}

uint8_t mai2_hid_benchmark_send_report(uint16_t sequence, uint64_t event_cycles, uint64_t tx_cycles, uint32_t core_hz)
{
  uint8_t report[MAI2_HID_REPORT_SIZE] = {0};
  uint8_t status;

  report[0] = 0x00;
  report[1] = 0xFF;
  report[2] = (uint8_t)(sequence & 0xFF);
  report[3] = (uint8_t)((sequence >> 8) & 0xFF);
  for (uint8_t i = 0; i < 8; i++) {
    report[4 + i] = (uint8_t)((event_cycles >> (i * 8)) & 0xFF);
    report[12 + i] = (uint8_t)((tx_cycles >> (i * 8)) & 0xFF);
  }
  report[20] = (uint8_t)(core_hz & 0xFF);
  report[21] = (uint8_t)((core_hz >> 8) & 0xFF);
  report[22] = (uint8_t)((core_hz >> 16) & 0xFF);
  report[23] = (uint8_t)((core_hz >> 24) & 0xFF);

  if (UsbTxGuard_Take(0u) == 0u) {
    return (uint8_t)USBD_BUSY;
  }

  status = USBD_CUSTOM_HID_SendReport(&hUsbDevice, report, sizeof(report));
  UsbTxGuard_Give();
  return status;
}

void mai2_hid_raw_debug_reset(void)
{
  raw_debug_sequence = 0u;
  raw_debug_part_index = 0u;
  memset(raw_debug_snapshot, 0, sizeof(raw_debug_snapshot));
}

uint8_t mai2_hid_raw_debug_stream(const uint16_t *raw_values, uint8_t value_count)
{
  uint8_t report[MAI2_HID_REPORT_SIZE] = {0};
  uint8_t total_parts;
  uint8_t first_value;
  uint8_t report_value_count;
  uint8_t part_index = raw_debug_part_index;
  uint8_t status;

  if ((raw_values == NULL) ||
      (value_count == 0u) ||
      (value_count > MAI2_HID_RAW_DEBUG_CHANNEL_COUNT)) {
    return (uint8_t)USBD_FAIL;
  }

  total_parts = (uint8_t)((value_count + MAI2_HID_RAW_DEBUG_VALUES_PER_REPORT - 1u) /
      MAI2_HID_RAW_DEBUG_VALUES_PER_REPORT);
  if (total_parts == 0u) {
    return (uint8_t)USBD_FAIL;
  }

  if (part_index >= total_parts) {
    part_index = 0u;
    raw_debug_part_index = 0u;
  }

  if (part_index == 0u) {
    memcpy(raw_debug_snapshot, raw_values, (size_t)value_count * sizeof(uint16_t));
  }

  first_value = (uint8_t)(part_index * MAI2_HID_RAW_DEBUG_VALUES_PER_REPORT);
  if (first_value >= value_count) {
    raw_debug_part_index = 0u;
    return (uint8_t)USBD_FAIL;
  }

  report_value_count = (uint8_t)(value_count - first_value);
  if (report_value_count > MAI2_HID_RAW_DEBUG_VALUES_PER_REPORT) {
    report_value_count = MAI2_HID_RAW_DEBUG_VALUES_PER_REPORT;
  }

  report[0] = MAI2_HID_RAW_DEBUG_MAGIC0;
  report[1] = MAI2_HID_RAW_DEBUG_MAGIC1;
  report[2] = raw_debug_sequence;
  report[3] = part_index;
  report[4] = report_value_count;

  for (uint8_t i = 0u; i < report_value_count; i++) {
    uint16_t raw = raw_debug_snapshot[first_value + i];
    report[5u + (i * 2u)] = (uint8_t)(raw & 0xFFu);
    report[6u + (i * 2u)] = (uint8_t)((raw >> 8) & 0xFFu);
  }

  if (UsbTxGuard_Take(0u) == 0u) {
    return (uint8_t)USBD_BUSY;
  }

  status = USBD_CUSTOM_HID_SendReport(&hUsbDevice, report, sizeof(report));
  UsbTxGuard_Give();
  if (status != (uint8_t)USBD_OK) {
    return status;
  }

  raw_debug_part_index++;
  if (raw_debug_part_index >= total_parts) {
    raw_debug_part_index = 0u;
    raw_debug_sequence++;
  }

  return status;
}
