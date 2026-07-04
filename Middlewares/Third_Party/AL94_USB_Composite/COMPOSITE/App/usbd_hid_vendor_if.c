/**
  ******************************************************************************
  * @file           : usbd_hid_vendor_if.c
  * @brief          : USB vendor command HID interface.
  ******************************************************************************
  */

#include "usbd_hid_vendor_if.h"

#include "serial_protocol.h"
#include "usbd_cdc_acm_if.h"
#include "serial_commands.h"

__ALIGN_BEGIN static uint8_t VENDOR_HID_ReportDesc[USBD_VENDOR_HID_REPORT_DESC_SIZE] __ALIGN_END =
{
    0x06, 0xCA, 0xFF,
    0x09, 0x02,
    0xA1, 0x01,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x40,
    0x09, 0x01,
    0x81, 0x02,
    0x95, 0x40,
    0x09, 0x01,
    0x91, 0x02,
    0xC0,
};

extern USBD_HandleTypeDef hUsbDevice;

static int8_t VENDOR_HID_Init(void);
static int8_t VENDOR_HID_DeInit(void);
static int8_t VENDOR_HID_OutEvent(const uint8_t *report, uint16_t len);

USBD_VENDOR_HID_ItfTypeDef USBD_VendorHID_fops = {
    VENDOR_HID_ReportDesc,
    VENDOR_HID_Init,
    VENDOR_HID_DeInit,
    VENDOR_HID_OutEvent,
};

static int8_t VENDOR_HID_Init(void)
{
  return (int8_t)USBD_OK;
}

static int8_t VENDOR_HID_DeInit(void)
{
  return (int8_t)USBD_OK;
}

static int8_t VENDOR_HID_OutEvent(const uint8_t *report, uint16_t len)
{
  if (serial_command_feed_isr_transport(report, len,
      SERIAL_COMMAND_TRANSPORT_VENDOR_HID) != 0u) {
    command_notify_ready_from_isr();
  }
  return (int8_t)USBD_OK;
}

uint8_t mai2_hid_vendor_ready(void)
{
  return USBD_VENDOR_HID_IsReady(&hUsbDevice);
}

uint8_t mai2_hid_vendor_send_report(uint8_t *report, uint16_t len)
{
  uint8_t status;

  if (UsbTxGuard_Take(0u) == 0u) {
    return (uint8_t)USBD_BUSY;
  }

  status = USBD_VENDOR_HID_SendReport(&hUsbDevice, report, len);
  UsbTxGuard_Give();
  return status;
}

uint8_t mai2_hid_vendor_abort(void)
{
  uint8_t status;

  if (UsbTxGuard_Take(0u) == 0u) {
    return (uint8_t)USBD_BUSY;
  }

  status = USBD_VENDOR_HID_AbortIn(&hUsbDevice);
  UsbTxGuard_Give();
  return status;
}
