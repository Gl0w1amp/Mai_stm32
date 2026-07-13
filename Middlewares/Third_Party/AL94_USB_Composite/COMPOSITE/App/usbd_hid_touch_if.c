/**
  ******************************************************************************
  * @file           : usbd_hid_touch_if.c
  * @brief          : USB maimai touch-stream HID interface.
  ******************************************************************************
  */

#include "usbd_hid_touch_if.h"
#include "usbd_cdc_acm_if.h"

extern USBD_HandleTypeDef hUsbDevice;

uint8_t mai2_hid_touch_ready(void)
{
  return USBD_TOUCH_HID_IsReady(&hUsbDevice);
}

uint8_t mai2_hid_touch_send_report(uint8_t *report, uint16_t len)
{
  uint8_t status;

  if (UsbTxGuard_Take(0u) == 0u) {
    return (uint8_t)USBD_BUSY;
  }

  status = USBD_TOUCH_HID_SendReport(&hUsbDevice, report, len);
  UsbTxGuard_Give();
  return status;
}

uint8_t mai2_hid_touch_abort(void)
{
  uint8_t status;

  if (UsbTxGuard_Take(0u) == 0u) {
    return (uint8_t)USBD_BUSY;
  }

  status = USBD_TOUCH_HID_AbortIn(&hUsbDevice);
  UsbTxGuard_Give();
  return status;
}
