/**
  ******************************************************************************
  * @file           : usbd_hid_touch_if.h
  * @brief          : Header for USB maimai touch-stream HID interface.
  ******************************************************************************
  */

#ifndef __USBD_TOUCH_HID_IF_H__
#define __USBD_TOUCH_HID_IF_H__

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>

#include "../Class/HID_TOUCH/Inc/usbd_hid_touch.h"

uint8_t mai2_hid_touch_ready(void);
uint8_t mai2_hid_touch_send_report(uint8_t *report, uint16_t len);
uint8_t mai2_hid_touch_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_TOUCH_HID_IF_H__ */
