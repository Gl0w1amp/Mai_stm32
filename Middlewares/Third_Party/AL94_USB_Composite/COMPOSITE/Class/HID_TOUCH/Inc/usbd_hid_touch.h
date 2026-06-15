/**
  ******************************************************************************
  * @file    usbd_hid_touch.h
  * @brief   Header file for the maimai touch-stream HID class.
  ******************************************************************************
  */

#ifndef __USB_TOUCH_HID_H
#define __USB_TOUCH_HID_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"

#define TOUCH_HID_STR_DESC                          "Curva Mai Touch Stream"

#define TOUCH_HID_EPIN_SIZE                         0x40U

#define USB_TOUCH_HID_CONFIG_DESC_SIZ               34U
#define USB_TOUCH_HID_DESC_SIZ                      9U

#ifndef TOUCH_HID_HS_BINTERVAL
#define TOUCH_HID_HS_BINTERVAL                      0x01U
#endif

#ifndef TOUCH_HID_FS_BINTERVAL
#define TOUCH_HID_FS_BINTERVAL                      0x01U
#endif

#ifndef USBD_TOUCH_HID_REPORT_DESC_SIZE
#define USBD_TOUCH_HID_REPORT_DESC_SIZE             23U
#endif

#ifndef USBD_TOUCH_HID_REPORT_SIZE
#define USBD_TOUCH_HID_REPORT_SIZE                  64U
#endif

#define TOUCH_HID_REPORT_ID                         0x31U
#define TOUCH_HID_DESCRIPTOR_TYPE                   0x21U
#define TOUCH_HID_REPORT_DESC                       0x22U

#define TOUCH_HID_REQ_SET_PROTOCOL                  0x0BU
#define TOUCH_HID_REQ_GET_PROTOCOL                  0x03U
#define TOUCH_HID_REQ_SET_IDLE                      0x0AU
#define TOUCH_HID_REQ_GET_IDLE                      0x02U
#define TOUCH_HID_REQ_SET_REPORT                    0x09U
#define TOUCH_HID_REQ_GET_REPORT                    0x01U

typedef enum
{
  TOUCH_HID_IDLE = 0U,
  TOUCH_HID_BUSY,
} TOUCH_HID_StateTypeDef;

typedef struct
{
  uint8_t Report_buf[USBD_TOUCH_HID_REPORT_SIZE];
  uint32_t Protocol;
  uint32_t IdleState;
  uint32_t AltSetting;
  uint32_t IsReportAvailable;
  TOUCH_HID_StateTypeDef state;
} USBD_TOUCH_HID_HandleTypeDef;

extern USBD_ClassTypeDef USBD_HID_TOUCH;

extern uint8_t TOUCH_HID_IN_EP;
extern uint8_t TOUCH_HID_ITF_NBR;
extern uint8_t TOUCH_HID_STR_DESC_IDX;

uint8_t USBD_TOUCH_HID_IsReady(USBD_HandleTypeDef *pdev);
uint8_t USBD_TOUCH_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len);
void USBD_Update_HID_Touch_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep, uint8_t str_idx);

#ifdef __cplusplus
}
#endif

#endif /* __USB_TOUCH_HID_H */
