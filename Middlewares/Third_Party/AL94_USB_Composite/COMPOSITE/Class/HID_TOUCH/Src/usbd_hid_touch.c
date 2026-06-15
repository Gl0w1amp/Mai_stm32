/**
  ******************************************************************************
  * @file    usbd_hid_touch.c
  * @brief   USB maimai touch-stream HID class implementation.
  ******************************************************************************
  */

#include "../Inc/usbd_hid_touch.h"
#include "usbd_ctlreq.h"
#include "usb_reporter.h"

#define _TOUCH_HID_IN_EP 0x81U
#define _TOUCH_HID_ITF_NBR 0x00U
#define _TOUCH_HID_STR_DESC_IDX 0x00U

uint8_t TOUCH_HID_IN_EP = _TOUCH_HID_IN_EP;
uint8_t TOUCH_HID_ITF_NBR = _TOUCH_HID_ITF_NBR;
uint8_t TOUCH_HID_STR_DESC_IDX = _TOUCH_HID_STR_DESC_IDX;

static uint8_t USBD_TOUCH_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_TOUCH_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_TOUCH_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_TOUCH_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_TOUCH_HID_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t *USBD_TOUCH_HID_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_TOUCH_HID_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_TOUCH_HID_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_TOUCH_HID_GetDeviceQualifierDesc(uint16_t *length);

static USBD_TOUCH_HID_HandleTypeDef TOUCH_HID_Instance;

USBD_ClassTypeDef USBD_HID_TOUCH =
    {
        USBD_TOUCH_HID_Init,
        USBD_TOUCH_HID_DeInit,
        USBD_TOUCH_HID_Setup,
        NULL,
        USBD_TOUCH_HID_EP0_RxReady,
        USBD_TOUCH_HID_DataIn,
        NULL,
        NULL,
        NULL,
        NULL,
        USBD_TOUCH_HID_GetHSCfgDesc,
        USBD_TOUCH_HID_GetFSCfgDesc,
        USBD_TOUCH_HID_GetOtherSpeedCfgDesc,
        USBD_TOUCH_HID_GetDeviceQualifierDesc,
};

__ALIGN_BEGIN static uint8_t TOUCH_HID_ReportDesc[USBD_TOUCH_HID_REPORT_DESC_SIZE] __ALIGN_END =
{
    0x06, 0x00, 0xFF,
    0x09, 0x31,
    0xA1, 0x01,
    0x85, TOUCH_HID_REPORT_ID,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x3F,
    0x09, 0x01,
    0x81, 0x02,
    0xC0,
};

__ALIGN_BEGIN static uint8_t USBD_TOUCH_HID_CfgFSDesc[USB_TOUCH_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_TOUCH_HID_CONFIG_DESC_SIZ,
        0x00,
        0x01,
        0x01,
        0x00,
#if (USBD_SELF_POWERED == 1U)
        0xC0,
#else
        0x80,
#endif
        USBD_MAX_POWER,
        0x09,
        USB_DESC_TYPE_INTERFACE,
        _TOUCH_HID_ITF_NBR,
        0x00,
        0x01,
        0x03,
        0x00,
        0x00,
        _TOUCH_HID_STR_DESC_IDX,
        0x09,
        TOUCH_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        TOUCH_HID_REPORT_DESC,
        USBD_TOUCH_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _TOUCH_HID_IN_EP,
        0x03,
        TOUCH_HID_EPIN_SIZE,
        0x00,
        TOUCH_HID_FS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_TOUCH_HID_CfgHSDesc[USB_TOUCH_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_TOUCH_HID_CONFIG_DESC_SIZ,
        0x00,
        0x01,
        0x01,
        0x00,
#if (USBD_SELF_POWERED == 1U)
        0xC0,
#else
        0x80,
#endif
        USBD_MAX_POWER,
        0x09,
        USB_DESC_TYPE_INTERFACE,
        _TOUCH_HID_ITF_NBR,
        0x00,
        0x01,
        0x03,
        0x00,
        0x00,
        _TOUCH_HID_STR_DESC_IDX,
        0x09,
        TOUCH_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        TOUCH_HID_REPORT_DESC,
        USBD_TOUCH_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _TOUCH_HID_IN_EP,
        0x03,
        TOUCH_HID_EPIN_SIZE,
        0x00,
        TOUCH_HID_HS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_TOUCH_HID_OtherSpeedCfgDesc[USB_TOUCH_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_TOUCH_HID_CONFIG_DESC_SIZ,
        0x00,
        0x01,
        0x01,
        0x00,
#if (USBD_SELF_POWERED == 1U)
        0xC0,
#else
        0x80,
#endif
        USBD_MAX_POWER,
        0x09,
        USB_DESC_TYPE_INTERFACE,
        _TOUCH_HID_ITF_NBR,
        0x00,
        0x01,
        0x03,
        0x00,
        0x00,
        _TOUCH_HID_STR_DESC_IDX,
        0x09,
        TOUCH_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        TOUCH_HID_REPORT_DESC,
        USBD_TOUCH_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _TOUCH_HID_IN_EP,
        0x03,
        TOUCH_HID_EPIN_SIZE,
        0x00,
        TOUCH_HID_FS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_TOUCH_HID_Desc[USB_TOUCH_HID_DESC_SIZ] __ALIGN_END =
{
        0x09,
        TOUCH_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        TOUCH_HID_REPORT_DESC,
        USBD_TOUCH_HID_REPORT_DESC_SIZE,
        0x00,
};

__ALIGN_BEGIN static uint8_t USBD_TOUCH_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
    {
        USB_LEN_DEV_QUALIFIER_DESC,
        USB_DESC_TYPE_DEVICE_QUALIFIER,
        0x00,
        0x02,
        0x00,
        0x00,
        0x00,
        0x40,
        0x01,
        0x00,
};

static uint8_t USBD_TOUCH_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    pdev->ep_in[TOUCH_HID_IN_EP & 0xFU].bInterval = TOUCH_HID_HS_BINTERVAL;
  }
  else
  {
    pdev->ep_in[TOUCH_HID_IN_EP & 0xFU].bInterval = TOUCH_HID_FS_BINTERVAL;
  }

  (void)USBD_LL_OpenEP(pdev, TOUCH_HID_IN_EP, USBD_EP_TYPE_INTR, TOUCH_HID_EPIN_SIZE);
  pdev->ep_in[TOUCH_HID_IN_EP & 0xFU].is_used = 1U;
  TOUCH_HID_Instance.state = TOUCH_HID_IDLE;
  TOUCH_HID_Instance.Protocol = 0U;
  TOUCH_HID_Instance.IdleState = 0U;
  TOUCH_HID_Instance.AltSetting = 0U;
  TOUCH_HID_Instance.IsReportAvailable = 0U;

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_TOUCH_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  (void)USBD_LL_CloseEP(pdev, TOUCH_HID_IN_EP);
  pdev->ep_in[TOUCH_HID_IN_EP & 0xFU].is_used = 0U;
  pdev->ep_in[TOUCH_HID_IN_EP & 0xFU].bInterval = 0U;
  TOUCH_HID_Instance.state = TOUCH_HID_IDLE;

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_TOUCH_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  uint16_t len = 0U;
  uint8_t *pbuf = NULL;
  uint16_t status_info = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
  case USB_REQ_TYPE_CLASS:
    switch (req->bRequest)
    {
    case TOUCH_HID_REQ_SET_PROTOCOL:
      TOUCH_HID_Instance.Protocol = (uint8_t)(req->wValue);
      break;
    case TOUCH_HID_REQ_GET_PROTOCOL:
      (void)USBD_CtlSendData(pdev, (uint8_t *)&TOUCH_HID_Instance.Protocol, 1U);
      break;
    case TOUCH_HID_REQ_SET_IDLE:
      TOUCH_HID_Instance.IdleState = (uint8_t)(req->wValue >> 8);
      break;
    case TOUCH_HID_REQ_GET_IDLE:
      (void)USBD_CtlSendData(pdev, (uint8_t *)&TOUCH_HID_Instance.IdleState, 1U);
      break;
    case TOUCH_HID_REQ_SET_REPORT:
      if (req->wLength > USBD_TOUCH_HID_REPORT_SIZE)
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
        break;
      }
      TOUCH_HID_Instance.IsReportAvailable = 1U;
      len = req->wLength;
      (void)USBD_CtlPrepareRx(pdev, TOUCH_HID_Instance.Report_buf, len);
      break;
    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
    }
    break;

  case USB_REQ_TYPE_STANDARD:
    switch (req->bRequest)
    {
    case USB_REQ_GET_STATUS:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
      }
      else
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
      }
      break;
    case USB_REQ_GET_DESCRIPTOR:
      if ((req->wValue >> 8) == TOUCH_HID_REPORT_DESC)
      {
        len = MIN(USBD_TOUCH_HID_REPORT_DESC_SIZE, req->wLength);
        pbuf = TOUCH_HID_ReportDesc;
      }
      else if ((req->wValue >> 8) == TOUCH_HID_DESCRIPTOR_TYPE)
      {
        pbuf = USBD_TOUCH_HID_Desc;
        len = MIN(USB_TOUCH_HID_DESC_SIZ, req->wLength);
      }
      else
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
        break;
      }

      (void)USBD_CtlSendData(pdev, pbuf, len);
      break;
    case USB_REQ_GET_INTERFACE:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        (void)USBD_CtlSendData(pdev, (uint8_t *)&TOUCH_HID_Instance.AltSetting, 1U);
      }
      else
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
      }
      break;
    case USB_REQ_SET_INTERFACE:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        TOUCH_HID_Instance.AltSetting = (uint8_t)(req->wValue);
      }
      else
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
      }
      break;
    case USB_REQ_CLEAR_FEATURE:
      break;
    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
    }
    break;

  default:
    USBD_CtlError(pdev, req);
    ret = USBD_FAIL;
    break;
  }

  return (uint8_t)ret;
}

uint8_t USBD_TOUCH_HID_IsReady(USBD_HandleTypeDef *pdev)
{
  if ((pdev == NULL) || (pdev->dev_state != USBD_STATE_CONFIGURED))
  {
    return 0U;
  }

  return (uint8_t)(TOUCH_HID_Instance.state == TOUCH_HID_IDLE);
}

uint8_t USBD_TOUCH_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  USBD_StatusTypeDef status;

  if ((pdev == NULL) || (report == NULL) || (len > TOUCH_HID_EPIN_SIZE))
  {
    return (uint8_t)USBD_FAIL;
  }

  if (pdev->dev_state != USBD_STATE_CONFIGURED)
  {
    return (uint8_t)USBD_BUSY;
  }

  if (TOUCH_HID_Instance.state != TOUCH_HID_IDLE)
  {
    return (uint8_t)USBD_BUSY;
  }

  TOUCH_HID_Instance.state = TOUCH_HID_BUSY;
  status = USBD_LL_Transmit(pdev, TOUCH_HID_IN_EP, report, len);
  if (status != USBD_OK)
  {
    TOUCH_HID_Instance.state = TOUCH_HID_IDLE;
  }

  return (uint8_t)status;
}

static uint8_t *USBD_TOUCH_HID_GetFSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_TOUCH_HID_CfgFSDesc);
  return USBD_TOUCH_HID_CfgFSDesc;
}

static uint8_t *USBD_TOUCH_HID_GetHSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_TOUCH_HID_CfgHSDesc);
  return USBD_TOUCH_HID_CfgHSDesc;
}

static uint8_t *USBD_TOUCH_HID_GetOtherSpeedCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_TOUCH_HID_OtherSpeedCfgDesc);
  return USBD_TOUCH_HID_OtherSpeedCfgDesc;
}

static uint8_t USBD_TOUCH_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(pdev);
  UNUSED(epnum);
  TOUCH_HID_Instance.state = TOUCH_HID_IDLE;
  usb_reporter_notify_touch_hid_in_complete();
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_TOUCH_HID_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);
  TOUCH_HID_Instance.IsReportAvailable = 0U;
  return (uint8_t)USBD_OK;
}

static uint8_t *USBD_TOUCH_HID_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_TOUCH_HID_DeviceQualifierDesc);
  return USBD_TOUCH_HID_DeviceQualifierDesc;
}

void USBD_Update_HID_Touch_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep, uint8_t str_idx)
{
  desc[11] = itf_no;
  desc[17] = str_idx;
  desc[29] = in_ep;

  TOUCH_HID_IN_EP = in_ep;
  TOUCH_HID_ITF_NBR = itf_no;
  TOUCH_HID_STR_DESC_IDX = str_idx;
}
