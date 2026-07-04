/**
  ******************************************************************************
  * @file    usbd_hid_vendor.c
  * @brief   USB vendor command HID class implementation.
  ******************************************************************************
  */

#include "../Inc/usbd_hid_vendor.h"
#include "usbd_ctlreq.h"
#include "usb_reporter.h"

#define _VENDOR_HID_IN_EP 0x81U
#define _VENDOR_HID_OUT_EP 0x01U
#define _VENDOR_HID_ITF_NBR 0x00U
#define _VENDOR_HID_STR_DESC_IDX 0x00U

uint8_t VENDOR_HID_IN_EP = _VENDOR_HID_IN_EP;
uint8_t VENDOR_HID_OUT_EP = _VENDOR_HID_OUT_EP;
uint8_t VENDOR_HID_ITF_NBR = _VENDOR_HID_ITF_NBR;
uint8_t VENDOR_HID_STR_DESC_IDX = _VENDOR_HID_STR_DESC_IDX;

static uint8_t USBD_VENDOR_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_VENDOR_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_VENDOR_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_VENDOR_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_VENDOR_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_VENDOR_HID_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t *USBD_VENDOR_HID_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_VENDOR_HID_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_VENDOR_HID_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_VENDOR_HID_GetDeviceQualifierDesc(uint16_t *length);

static USBD_VENDOR_HID_HandleTypeDef VENDOR_HID_Instance;

USBD_ClassTypeDef USBD_HID_VENDOR =
    {
        USBD_VENDOR_HID_Init,
        USBD_VENDOR_HID_DeInit,
        USBD_VENDOR_HID_Setup,
        NULL,
        USBD_VENDOR_HID_EP0_RxReady,
        USBD_VENDOR_HID_DataIn,
        USBD_VENDOR_HID_DataOut,
        NULL,
        NULL,
        NULL,
        USBD_VENDOR_HID_GetHSCfgDesc,
        USBD_VENDOR_HID_GetFSCfgDesc,
        USBD_VENDOR_HID_GetOtherSpeedCfgDesc,
        USBD_VENDOR_HID_GetDeviceQualifierDesc,
};

__ALIGN_BEGIN static uint8_t USBD_VENDOR_HID_CfgFSDesc[USB_VENDOR_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_VENDOR_HID_CONFIG_DESC_SIZ,
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
        _VENDOR_HID_ITF_NBR,
        0x00,
        0x02,
        0x03,
        0x00,
        0x00,
        _VENDOR_HID_STR_DESC_IDX,
        0x09,
        VENDOR_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        VENDOR_HID_REPORT_DESC,
        USBD_VENDOR_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _VENDOR_HID_IN_EP,
        0x03,
        VENDOR_HID_EPIN_SIZE,
        0x00,
        VENDOR_HID_FS_BINTERVAL,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _VENDOR_HID_OUT_EP,
        0x03,
        VENDOR_HID_EPOUT_SIZE,
        0x00,
        VENDOR_HID_FS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_VENDOR_HID_CfgHSDesc[USB_VENDOR_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_VENDOR_HID_CONFIG_DESC_SIZ,
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
        _VENDOR_HID_ITF_NBR,
        0x00,
        0x02,
        0x03,
        0x00,
        0x00,
        _VENDOR_HID_STR_DESC_IDX,
        0x09,
        VENDOR_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        VENDOR_HID_REPORT_DESC,
        USBD_VENDOR_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _VENDOR_HID_IN_EP,
        0x03,
        VENDOR_HID_EPIN_SIZE,
        0x00,
        VENDOR_HID_HS_BINTERVAL,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _VENDOR_HID_OUT_EP,
        0x03,
        VENDOR_HID_EPOUT_SIZE,
        0x00,
        VENDOR_HID_HS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_VENDOR_HID_OtherSpeedCfgDesc[USB_VENDOR_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_VENDOR_HID_CONFIG_DESC_SIZ,
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
        _VENDOR_HID_ITF_NBR,
        0x00,
        0x02,
        0x03,
        0x00,
        0x00,
        _VENDOR_HID_STR_DESC_IDX,
        0x09,
        VENDOR_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        VENDOR_HID_REPORT_DESC,
        USBD_VENDOR_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _VENDOR_HID_IN_EP,
        0x03,
        VENDOR_HID_EPIN_SIZE,
        0x00,
        VENDOR_HID_FS_BINTERVAL,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _VENDOR_HID_OUT_EP,
        0x03,
        VENDOR_HID_EPOUT_SIZE,
        0x00,
        VENDOR_HID_FS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_VENDOR_HID_Desc[USB_VENDOR_HID_DESC_SIZ] __ALIGN_END =
{
        0x09,
        VENDOR_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        VENDOR_HID_REPORT_DESC,
        USBD_VENDOR_HID_REPORT_DESC_SIZE,
        0x00,
};

__ALIGN_BEGIN static uint8_t USBD_VENDOR_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
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

static uint8_t USBD_VENDOR_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  USBD_VENDOR_HID_HandleTypeDef *hhid = &VENDOR_HID_Instance;

  pdev->pClassData_HID_Vendor = (void *)hhid;

  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    pdev->ep_in[VENDOR_HID_IN_EP & 0xFU].bInterval = VENDOR_HID_HS_BINTERVAL;
    pdev->ep_out[VENDOR_HID_OUT_EP & 0xFU].bInterval = VENDOR_HID_HS_BINTERVAL;
  }
  else
  {
    pdev->ep_in[VENDOR_HID_IN_EP & 0xFU].bInterval = VENDOR_HID_FS_BINTERVAL;
    pdev->ep_out[VENDOR_HID_OUT_EP & 0xFU].bInterval = VENDOR_HID_FS_BINTERVAL;
  }

  (void)USBD_LL_OpenEP(pdev, VENDOR_HID_IN_EP, USBD_EP_TYPE_INTR, VENDOR_HID_EPIN_SIZE);
  pdev->ep_in[VENDOR_HID_IN_EP & 0xFU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, VENDOR_HID_OUT_EP, USBD_EP_TYPE_INTR, VENDOR_HID_EPOUT_SIZE);
  pdev->ep_out[VENDOR_HID_OUT_EP & 0xFU].is_used = 1U;

  hhid->state = VENDOR_HID_IDLE;
  hhid->Protocol = 0U;
  hhid->IdleState = 0U;
  hhid->AltSetting = 0U;
  hhid->IsReportAvailable = 0U;

  ((USBD_VENDOR_HID_ItfTypeDef *)pdev->pUserData_HID_Vendor)->Init();
  (void)USBD_LL_PrepareReceive(pdev, VENDOR_HID_OUT_EP, hhid->Report_buf, USBD_VENDOR_HID_OUTREPORT_BUF_SIZE);

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_VENDOR_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  (void)USBD_LL_CloseEP(pdev, VENDOR_HID_IN_EP);
  pdev->ep_in[VENDOR_HID_IN_EP & 0xFU].is_used = 0U;
  pdev->ep_in[VENDOR_HID_IN_EP & 0xFU].bInterval = 0U;

  (void)USBD_LL_CloseEP(pdev, VENDOR_HID_OUT_EP);
  pdev->ep_out[VENDOR_HID_OUT_EP & 0xFU].is_used = 0U;
  pdev->ep_out[VENDOR_HID_OUT_EP & 0xFU].bInterval = 0U;

  if (pdev->pClassData_HID_Vendor != NULL)
  {
    ((USBD_VENDOR_HID_ItfTypeDef *)pdev->pUserData_HID_Vendor)->DeInit();
    pdev->pClassData_HID_Vendor = NULL;
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_VENDOR_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_VENDOR_HID_HandleTypeDef *hhid = (USBD_VENDOR_HID_HandleTypeDef *)pdev->pClassData_HID_Vendor;
  uint16_t len = 0U;
  uint8_t *pbuf = NULL;
  uint16_t status_info = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
  case USB_REQ_TYPE_CLASS:
    switch (req->bRequest)
    {
    case VENDOR_HID_REQ_SET_PROTOCOL:
      hhid->Protocol = (uint8_t)(req->wValue);
      break;
    case VENDOR_HID_REQ_GET_PROTOCOL:
      (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->Protocol, 1U);
      break;
    case VENDOR_HID_REQ_SET_IDLE:
      hhid->IdleState = (uint8_t)(req->wValue >> 8);
      break;
    case VENDOR_HID_REQ_GET_IDLE:
      (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->IdleState, 1U);
      break;
    case VENDOR_HID_REQ_SET_REPORT:
      if (req->wLength > USBD_VENDOR_HID_OUTREPORT_BUF_SIZE)
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
        break;
      }
      hhid->IsReportAvailable = 1U;
      len = req->wLength;
      (void)USBD_CtlPrepareRx(pdev, hhid->Report_buf, len);
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
      if ((req->wValue >> 8) == VENDOR_HID_REPORT_DESC)
      {
        len = MIN(USBD_VENDOR_HID_REPORT_DESC_SIZE, req->wLength);
        pbuf = ((USBD_VENDOR_HID_ItfTypeDef *)pdev->pUserData_HID_Vendor)->pReport;
      }
      else if ((req->wValue >> 8) == VENDOR_HID_DESCRIPTOR_TYPE)
      {
        pbuf = USBD_VENDOR_HID_Desc;
        len = MIN(USB_VENDOR_HID_DESC_SIZ, req->wLength);
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
        (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->AltSetting, 1U);
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
        hhid->AltSetting = (uint8_t)(req->wValue);
      }
      else
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
      }
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

uint8_t USBD_VENDOR_HID_IsReady(USBD_HandleTypeDef *pdev)
{
  if ((pdev == NULL) || (pdev->pClassData_HID_Vendor == NULL) ||
      (pdev->dev_state != USBD_STATE_CONFIGURED))
  {
    return 0U;
  }

  return (uint8_t)(VENDOR_HID_Instance.state == VENDOR_HID_IDLE);
}

uint8_t USBD_VENDOR_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  uint8_t status;

  if ((pdev == NULL) || (report == NULL) || (len > VENDOR_HID_EPIN_SIZE))
  {
    return (uint8_t)USBD_FAIL;
  }

  if (pdev->dev_state != USBD_STATE_CONFIGURED)
  {
    return (uint8_t)USBD_BUSY;
  }

  if (VENDOR_HID_Instance.state != VENDOR_HID_IDLE)
  {
    return (uint8_t)USBD_BUSY;
  }

  VENDOR_HID_Instance.state = VENDOR_HID_BUSY;
  status = USBD_LL_Transmit(pdev, VENDOR_HID_IN_EP, report, len);
  if (status != USBD_OK)
  {
    VENDOR_HID_Instance.state = VENDOR_HID_IDLE;
  }

  return (uint8_t)status;
}

/* Recover a stranded IN transfer: flush the endpoint and force the class state
 * back to IDLE. Called by the reporter's in-flight watchdog when a DataIn
 * completion is lost (bus reset / re-enumeration / peripheral fault). */
uint8_t USBD_VENDOR_HID_AbortIn(USBD_HandleTypeDef *pdev)
{
  if ((pdev == NULL) || (pdev->pClassData_HID_Vendor == NULL))
  {
    return (uint8_t)USBD_FAIL;
  }

  (void)USBD_LL_FlushEP(pdev, VENDOR_HID_IN_EP);
  ((USBD_VENDOR_HID_HandleTypeDef *)pdev->pClassData_HID_Vendor)->state = VENDOR_HID_IDLE;
  return (uint8_t)USBD_OK;
}

static uint8_t *USBD_VENDOR_HID_GetFSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_VENDOR_HID_CfgFSDesc);
  return USBD_VENDOR_HID_CfgFSDesc;
}

static uint8_t *USBD_VENDOR_HID_GetHSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_VENDOR_HID_CfgHSDesc);
  return USBD_VENDOR_HID_CfgHSDesc;
}

static uint8_t *USBD_VENDOR_HID_GetOtherSpeedCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_VENDOR_HID_OtherSpeedCfgDesc);
  return USBD_VENDOR_HID_OtherSpeedCfgDesc;
}

static uint8_t USBD_VENDOR_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(epnum);
  if (pdev->pClassData_HID_Vendor != NULL)
  {
    ((USBD_VENDOR_HID_HandleTypeDef *)pdev->pClassData_HID_Vendor)->state = VENDOR_HID_IDLE;
  }
  usb_reporter_notify_vendor_hid_in_complete();
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_VENDOR_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_VENDOR_HID_HandleTypeDef *hhid;
  UNUSED(epnum);

  if (pdev->pClassData_HID_Vendor == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  hhid = (USBD_VENDOR_HID_HandleTypeDef *)pdev->pClassData_HID_Vendor;
  ((USBD_VENDOR_HID_ItfTypeDef *)pdev->pUserData_HID_Vendor)->OutEvent(
      hhid->Report_buf,
      USBD_VENDOR_HID_OUTREPORT_BUF_SIZE);

  (void)USBD_LL_PrepareReceive(
      pdev,
      VENDOR_HID_OUT_EP,
      hhid->Report_buf,
      USBD_VENDOR_HID_OUTREPORT_BUF_SIZE);

  return (uint8_t)USBD_OK;
}

uint8_t USBD_VENDOR_HID_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  USBD_VENDOR_HID_HandleTypeDef *hhid;

  if (pdev->pClassData_HID_Vendor == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  hhid = (USBD_VENDOR_HID_HandleTypeDef *)pdev->pClassData_HID_Vendor;
  (void)USBD_LL_PrepareReceive(pdev, VENDOR_HID_OUT_EP, hhid->Report_buf, USBD_VENDOR_HID_OUTREPORT_BUF_SIZE);
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_VENDOR_HID_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_VENDOR_HID_HandleTypeDef *hhid = (USBD_VENDOR_HID_HandleTypeDef *)pdev->pClassData_HID_Vendor;

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (hhid->IsReportAvailable == 1U)
  {
    ((USBD_VENDOR_HID_ItfTypeDef *)pdev->pUserData_HID_Vendor)->OutEvent(hhid->Report_buf, USBD_VENDOR_HID_OUTREPORT_BUF_SIZE);
    hhid->IsReportAvailable = 0U;
  }

  return (uint8_t)USBD_OK;
}

static uint8_t *USBD_VENDOR_HID_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_VENDOR_HID_DeviceQualifierDesc);
  return USBD_VENDOR_HID_DeviceQualifierDesc;
}

uint8_t USBD_VENDOR_HID_RegisterInterface(USBD_HandleTypeDef *pdev, USBD_VENDOR_HID_ItfTypeDef *fops)
{
  if (fops == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  pdev->pUserData_HID_Vendor = fops;
  return (uint8_t)USBD_OK;
}

void USBD_Update_HID_Vendor_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep, uint8_t out_ep, uint8_t str_idx)
{
  desc[11] = itf_no;
  desc[17] = str_idx;
  desc[29] = in_ep;
  desc[36] = out_ep;

  VENDOR_HID_IN_EP = in_ep;
  VENDOR_HID_OUT_EP = out_ep;
  VENDOR_HID_ITF_NBR = itf_no;
  VENDOR_HID_STR_DESC_IDX = str_idx;
}
