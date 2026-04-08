/**
  ******************************************************************************
  * @file    usbd_customhid.h
  * @brief   Header file for the custom HID class.
  ******************************************************************************
  */

#ifndef __USB_CUSTOMHID_H
#define __USB_CUSTOMHID_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"

#define CUSTOM_HID_STR_DESC                          "Curva Mai Buttons"

#define CUSTOM_HID_EPIN_SIZE                         0x18U
#define CUSTOM_HID_EPOUT_SIZE                        0x18U

#define USB_CUSTOM_HID_CONFIG_DESC_SIZ               41U
#define USB_CUSTOM_HID_DESC_SIZ                      9U

#ifndef CUSTOM_HID_HS_BINTERVAL
#define CUSTOM_HID_HS_BINTERVAL                      0x01U
#endif

#ifndef CUSTOM_HID_FS_BINTERVAL
#define CUSTOM_HID_FS_BINTERVAL                      0x01U
#endif

#ifndef USBD_CUSTOMHID_OUTREPORT_BUF_SIZE
#define USBD_CUSTOMHID_OUTREPORT_BUF_SIZE            0x18U
#endif

#ifndef USBD_CUSTOM_HID_REPORT_DESC_SIZE
#define USBD_CUSTOM_HID_REPORT_DESC_SIZE             21U
#endif

#define CUSTOM_HID_DESCRIPTOR_TYPE                   0x21U
#define CUSTOM_HID_REPORT_DESC                       0x22U

#define CUSTOM_HID_REQ_SET_PROTOCOL                  0x0BU
#define CUSTOM_HID_REQ_GET_PROTOCOL                  0x03U
#define CUSTOM_HID_REQ_SET_IDLE                      0x0AU
#define CUSTOM_HID_REQ_GET_IDLE                      0x02U
#define CUSTOM_HID_REQ_SET_REPORT                    0x09U
#define CUSTOM_HID_REQ_GET_REPORT                    0x01U

typedef enum
{
  CUSTOM_HID_IDLE = 0U,
  CUSTOM_HID_BUSY,
} CUSTOM_HID_StateTypeDef;

typedef struct _USBD_CUSTOM_HID_Itf
{
  uint8_t *pReport;
  int8_t (*Init)(void);
  int8_t (*DeInit)(void);
  int8_t (*OutEvent)(uint8_t event_idx, uint8_t state);
} USBD_CUSTOM_HID_ItfTypeDef;

typedef struct
{
  uint8_t Report_buf[USBD_CUSTOMHID_OUTREPORT_BUF_SIZE];
  uint32_t Protocol;
  uint32_t IdleState;
  uint32_t AltSetting;
  uint32_t IsReportAvailable;
  CUSTOM_HID_StateTypeDef state;
} USBD_CUSTOM_HID_HandleTypeDef;

extern USBD_ClassTypeDef USBD_HID_CUSTOM;

extern uint8_t CUSTOM_HID_IN_EP;
extern uint8_t CUSTOM_HID_OUT_EP;
extern uint8_t CUSTOM_HID_ITF_NBR;
extern uint8_t CUSTOM_HID_STR_DESC_IDX;

uint8_t USBD_CUSTOM_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len);
uint8_t USBD_CUSTOM_HID_ReceivePacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_CUSTOM_HID_RegisterInterface(USBD_HandleTypeDef *pdev, USBD_CUSTOM_HID_ItfTypeDef *fops);
void USBD_Update_HID_Custom_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep, uint8_t out_ep, uint8_t str_idx);

#ifdef __cplusplus
}
#endif

#endif /* __USB_CUSTOMHID_H */
