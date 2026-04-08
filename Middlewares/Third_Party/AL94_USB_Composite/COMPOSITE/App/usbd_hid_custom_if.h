/**
  ******************************************************************************
  * @file           : usbd_hid_custom_if.h
  * @brief          : Header for USB custom HID interface.
  ******************************************************************************
  */

#ifndef __USBD_CUSTOM_HID_IF_H__
#define __USBD_CUSTOM_HID_IF_H__

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>

#include "usbd_hid_custom.h"

extern USBD_CUSTOM_HID_ItfTypeDef USBD_CustomHID_fops;

uint8_t mai2_hid_buttons_send_report(uint8_t buttons0, uint8_t io_status);
uint8_t mai2_hid_benchmark_send_report(uint16_t sequence, uint64_t event_cycles, uint64_t tx_cycles, uint32_t core_hz);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CUSTOM_HID_IF_H__ */
