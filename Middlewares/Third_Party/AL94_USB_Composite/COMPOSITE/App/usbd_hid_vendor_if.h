/**
  ******************************************************************************
  * @file           : usbd_hid_vendor_if.h
  * @brief          : USB vendor command HID interface.
  ******************************************************************************
  */

#ifndef __USBD_VENDOR_HID_IF_H__
#define __USBD_VENDOR_HID_IF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "../Class/HID_VENDOR/Inc/usbd_hid_vendor.h"

extern USBD_VENDOR_HID_ItfTypeDef USBD_VendorHID_fops;

uint8_t mai2_hid_vendor_ready(void);
uint8_t mai2_hid_vendor_send_report(uint8_t *report, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_VENDOR_HID_IF_H__ */
