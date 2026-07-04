/**
  ******************************************************************************
  * @file    usbd_hid_vendor.h
  * @brief   Header file for the vendor command HID class.
  ******************************************************************************
  */

#ifndef __USB_VENDOR_HID_H
#define __USB_VENDOR_HID_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"

#define VENDOR_HID_STR_DESC                          "Curva Mai Command"

#define VENDOR_HID_EPIN_SIZE                         0x40U
#define VENDOR_HID_EPOUT_SIZE                        0x40U

#define USB_VENDOR_HID_CONFIG_DESC_SIZ               41U
#define USB_VENDOR_HID_DESC_SIZ                      9U

#ifndef VENDOR_HID_HS_BINTERVAL
#define VENDOR_HID_HS_BINTERVAL                      0x01U
#endif

#ifndef VENDOR_HID_FS_BINTERVAL
#define VENDOR_HID_FS_BINTERVAL                      0x01U
#endif

#ifndef USBD_VENDOR_HID_OUTREPORT_BUF_SIZE
#define USBD_VENDOR_HID_OUTREPORT_BUF_SIZE           0x40U
#endif

#ifndef USBD_VENDOR_HID_REPORT_DESC_SIZE
#define USBD_VENDOR_HID_REPORT_DESC_SIZE             27U
#endif

#define VENDOR_HID_DESCRIPTOR_TYPE                   0x21U
#define VENDOR_HID_REPORT_DESC                       0x22U

#define VENDOR_HID_REQ_SET_PROTOCOL                  0x0BU
#define VENDOR_HID_REQ_GET_PROTOCOL                  0x03U
#define VENDOR_HID_REQ_SET_IDLE                      0x0AU
#define VENDOR_HID_REQ_GET_IDLE                      0x02U
#define VENDOR_HID_REQ_SET_REPORT                    0x09U
#define VENDOR_HID_REQ_GET_REPORT                    0x01U

typedef enum
{
  VENDOR_HID_IDLE = 0U,
  VENDOR_HID_BUSY,
} VENDOR_HID_StateTypeDef;

typedef struct _USBD_VENDOR_HID_Itf
{
  uint8_t *pReport;
  int8_t (*Init)(void);
  int8_t (*DeInit)(void);
  int8_t (*OutEvent)(const uint8_t *report, uint16_t len);
} USBD_VENDOR_HID_ItfTypeDef;

typedef struct
{
  uint8_t Report_buf[USBD_VENDOR_HID_OUTREPORT_BUF_SIZE];
  uint32_t Protocol;
  uint32_t IdleState;
  uint32_t AltSetting;
  uint32_t IsReportAvailable;
  VENDOR_HID_StateTypeDef state;
} USBD_VENDOR_HID_HandleTypeDef;

extern USBD_ClassTypeDef USBD_HID_VENDOR;

extern uint8_t VENDOR_HID_IN_EP;
extern uint8_t VENDOR_HID_OUT_EP;
extern uint8_t VENDOR_HID_ITF_NBR;
extern uint8_t VENDOR_HID_STR_DESC_IDX;

uint8_t USBD_VENDOR_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len);
uint8_t USBD_VENDOR_HID_IsReady(USBD_HandleTypeDef *pdev);
uint8_t USBD_VENDOR_HID_AbortIn(USBD_HandleTypeDef *pdev);
uint8_t USBD_VENDOR_HID_ReceivePacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_VENDOR_HID_RegisterInterface(USBD_HandleTypeDef *pdev, USBD_VENDOR_HID_ItfTypeDef *fops);
void USBD_Update_HID_Vendor_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep, uint8_t out_ep, uint8_t str_idx);

#ifdef __cplusplus
}
#endif

#endif /* __USB_VENDOR_HID_H */
