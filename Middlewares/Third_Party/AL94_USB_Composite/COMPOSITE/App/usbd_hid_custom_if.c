/**
  ******************************************************************************
  * @file           : usbd_hid_custom_if.c
  * @brief          : USB custom HID interface.
  ******************************************************************************
  */

#include "usbd_hid_custom_if.h"

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
  uint8_t report[24] = {0};

  report[0] = buttons0;
  report[1] = io_status;
  report[2] = (uint8_t)(sequence & 0xFF);
  report[3] = (uint8_t)((sequence >> 8) & 0xFF);
  sequence++;

  return USBD_CUSTOM_HID_SendReport(&hUsbDevice, report, sizeof(report));
}

uint8_t mai2_hid_benchmark_send_report(uint16_t sequence, uint64_t event_cycles, uint64_t tx_cycles, uint32_t core_hz)
{
  uint8_t report[24] = {0};

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

  return USBD_CUSTOM_HID_SendReport(&hUsbDevice, report, sizeof(report));
}
