/**
  ******************************************************************************
  * @file    usbd_customhid.c
  * @brief   USB custom HID class implementation.
  ******************************************************************************
  */

#include "usbd_hid_custom.h"
#include "usbd_ctlreq.h"

#define _CUSTOM_HID_IN_EP 0x81U
#define _CUSTOM_HID_OUT_EP 0x01U
#define _CUSTOM_HID_ITF_NBR 0x00U
#define _CUSTOM_HID_STR_DESC_IDX 0x00U

uint8_t CUSTOM_HID_IN_EP = _CUSTOM_HID_IN_EP;
uint8_t CUSTOM_HID_OUT_EP = _CUSTOM_HID_OUT_EP;
uint8_t CUSTOM_HID_ITF_NBR = _CUSTOM_HID_ITF_NBR;
uint8_t CUSTOM_HID_STR_DESC_IDX = _CUSTOM_HID_STR_DESC_IDX;

static uint8_t USBD_CUSTOM_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_CUSTOM_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_CUSTOM_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_CUSTOM_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_CUSTOM_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_CUSTOM_HID_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t *USBD_CUSTOM_HID_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_CUSTOM_HID_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_CUSTOM_HID_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_CUSTOM_HID_GetDeviceQualifierDesc(uint16_t *length);

static USBD_CUSTOM_HID_HandleTypeDef CUSTOM_HID_Instance;

USBD_ClassTypeDef USBD_HID_CUSTOM =
    {
        USBD_CUSTOM_HID_Init,
        USBD_CUSTOM_HID_DeInit,
        USBD_CUSTOM_HID_Setup,
        NULL,
        USBD_CUSTOM_HID_EP0_RxReady,
        USBD_CUSTOM_HID_DataIn,
        USBD_CUSTOM_HID_DataOut,
        NULL,
        NULL,
        NULL,
        USBD_CUSTOM_HID_GetHSCfgDesc,
        USBD_CUSTOM_HID_GetFSCfgDesc,
        USBD_CUSTOM_HID_GetOtherSpeedCfgDesc,
        USBD_CUSTOM_HID_GetDeviceQualifierDesc,
};

__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_CfgFSDesc[USB_CUSTOM_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_CUSTOM_HID_CONFIG_DESC_SIZ,
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
        _CUSTOM_HID_ITF_NBR,
        0x00,
        0x02,
        0x03,
        0x00,
        0x00,
        _CUSTOM_HID_STR_DESC_IDX,
        0x09,
        CUSTOM_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        0x22,
        USBD_CUSTOM_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _CUSTOM_HID_IN_EP,
        0x03,
        CUSTOM_HID_EPIN_SIZE,
        0x00,
        CUSTOM_HID_FS_BINTERVAL,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _CUSTOM_HID_OUT_EP,
        0x03,
        CUSTOM_HID_EPOUT_SIZE,
        0x00,
        CUSTOM_HID_FS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_CfgHSDesc[USB_CUSTOM_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_CUSTOM_HID_CONFIG_DESC_SIZ,
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
        _CUSTOM_HID_ITF_NBR,
        0x00,
        0x02,
        0x03,
        0x00,
        0x00,
        _CUSTOM_HID_STR_DESC_IDX,
        0x09,
        CUSTOM_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        0x22,
        USBD_CUSTOM_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _CUSTOM_HID_IN_EP,
        0x03,
        CUSTOM_HID_EPIN_SIZE,
        0x00,
        CUSTOM_HID_HS_BINTERVAL,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _CUSTOM_HID_OUT_EP,
        0x03,
        CUSTOM_HID_EPOUT_SIZE,
        0x00,
        CUSTOM_HID_HS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_OtherSpeedCfgDesc[USB_CUSTOM_HID_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        0x09,
        USB_DESC_TYPE_CONFIGURATION,
        USB_CUSTOM_HID_CONFIG_DESC_SIZ,
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
        _CUSTOM_HID_ITF_NBR,
        0x00,
        0x02,
        0x03,
        0x00,
        0x00,
        _CUSTOM_HID_STR_DESC_IDX,
        0x09,
        CUSTOM_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        0x22,
        USBD_CUSTOM_HID_REPORT_DESC_SIZE,
        0x00,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _CUSTOM_HID_IN_EP,
        0x03,
        CUSTOM_HID_EPIN_SIZE,
        0x00,
        CUSTOM_HID_FS_BINTERVAL,
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        _CUSTOM_HID_OUT_EP,
        0x03,
        CUSTOM_HID_EPOUT_SIZE,
        0x00,
        CUSTOM_HID_FS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_Desc[USB_CUSTOM_HID_DESC_SIZ] __ALIGN_END =
{
        0x09,
        CUSTOM_HID_DESCRIPTOR_TYPE,
        0x11,
        0x01,
        0x00,
        0x01,
        0x22,
        USBD_CUSTOM_HID_REPORT_DESC_SIZE,
        0x00,
};

__ALIGN_BEGIN static uint8_t USBD_CUSTOM_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
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

static uint8_t USBD_CUSTOM_HID_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  USBD_CUSTOM_HID_HandleTypeDef *hhid = &CUSTOM_HID_Instance;

  if (hhid == NULL)
  {
    pdev->pClassData_HID_Custom = NULL;
    return (uint8_t)USBD_EMEM;
  }

  pdev->pClassData_HID_Custom = (void *)hhid;

  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    pdev->ep_in[CUSTOM_HID_IN_EP & 0xFU].bInterval = CUSTOM_HID_HS_BINTERVAL;
    pdev->ep_out[CUSTOM_HID_OUT_EP & 0xFU].bInterval = CUSTOM_HID_HS_BINTERVAL;
  }
  else
  {
    pdev->ep_in[CUSTOM_HID_IN_EP & 0xFU].bInterval = CUSTOM_HID_FS_BINTERVAL;
    pdev->ep_out[CUSTOM_HID_OUT_EP & 0xFU].bInterval = CUSTOM_HID_FS_BINTERVAL;
  }

  (void)USBD_LL_OpenEP(pdev, CUSTOM_HID_IN_EP, USBD_EP_TYPE_INTR, CUSTOM_HID_EPIN_SIZE);
  pdev->ep_in[CUSTOM_HID_IN_EP & 0xFU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, CUSTOM_HID_OUT_EP, USBD_EP_TYPE_INTR, CUSTOM_HID_EPOUT_SIZE);
  pdev->ep_out[CUSTOM_HID_OUT_EP & 0xFU].is_used = 1U;

  hhid->state = CUSTOM_HID_IDLE;

  ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData_HID_Custom)->Init();
  (void)USBD_LL_PrepareReceive(pdev, CUSTOM_HID_OUT_EP, hhid->Report_buf, USBD_CUSTOMHID_OUTREPORT_BUF_SIZE);

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_CUSTOM_HID_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  (void)USBD_LL_CloseEP(pdev, CUSTOM_HID_IN_EP);
  pdev->ep_in[CUSTOM_HID_IN_EP & 0xFU].is_used = 0U;
  pdev->ep_in[CUSTOM_HID_IN_EP & 0xFU].bInterval = 0U;

  (void)USBD_LL_CloseEP(pdev, CUSTOM_HID_OUT_EP);
  pdev->ep_out[CUSTOM_HID_OUT_EP & 0xFU].is_used = 0U;
  pdev->ep_out[CUSTOM_HID_OUT_EP & 0xFU].bInterval = 0U;

  if (pdev->pClassData_HID_Custom != NULL)
  {
    ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData_HID_Custom)->DeInit();
    pdev->pClassData_HID_Custom = NULL;
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_CUSTOM_HID_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData_HID_Custom;
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
    case CUSTOM_HID_REQ_SET_PROTOCOL:
      hhid->Protocol = (uint8_t)(req->wValue);
      break;
    case CUSTOM_HID_REQ_GET_PROTOCOL:
      (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->Protocol, 1U);
      break;
    case CUSTOM_HID_REQ_SET_IDLE:
      hhid->IdleState = (uint8_t)(req->wValue >> 8);
      break;
    case CUSTOM_HID_REQ_GET_IDLE:
      (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->IdleState, 1U);
      break;
    case CUSTOM_HID_REQ_SET_REPORT:
      if (req->wLength > USBD_CUSTOMHID_OUTREPORT_BUF_SIZE)
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
      if ((req->wValue >> 8) == CUSTOM_HID_REPORT_DESC)
      {
        len = MIN(USBD_CUSTOM_HID_REPORT_DESC_SIZE, req->wLength);
        pbuf = ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData_HID_Custom)->pReport;
      }
      else if ((req->wValue >> 8) == CUSTOM_HID_DESCRIPTOR_TYPE)
      {
        pbuf = USBD_CUSTOM_HID_Desc;
        len = MIN(USB_CUSTOM_HID_DESC_SIZ, req->wLength);
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

uint8_t USBD_CUSTOM_HID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid;

  if (pdev->pClassData_HID_Custom == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData_HID_Custom;

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (hhid->state == CUSTOM_HID_IDLE)
    {
      hhid->state = CUSTOM_HID_BUSY;
      (void)USBD_LL_Transmit(pdev, CUSTOM_HID_IN_EP, report, len);
    }
    else
    {
      return (uint8_t)USBD_BUSY;
    }
  }

  return (uint8_t)USBD_OK;
}

static uint8_t *USBD_CUSTOM_HID_GetFSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_CUSTOM_HID_CfgFSDesc);
  return USBD_CUSTOM_HID_CfgFSDesc;
}

static uint8_t *USBD_CUSTOM_HID_GetHSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_CUSTOM_HID_CfgHSDesc);
  return USBD_CUSTOM_HID_CfgHSDesc;
}

static uint8_t *USBD_CUSTOM_HID_GetOtherSpeedCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_CUSTOM_HID_OtherSpeedCfgDesc);
  return USBD_CUSTOM_HID_OtherSpeedCfgDesc;
}

static uint8_t USBD_CUSTOM_HID_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(epnum);
  ((USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData_HID_Custom)->state = CUSTOM_HID_IDLE;
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_CUSTOM_HID_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(epnum);

  if (pdev->pClassData_HID_Custom == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  (void)USBD_LL_PrepareReceive(
      pdev,
      CUSTOM_HID_OUT_EP,
      ((USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData_HID_Custom)->Report_buf,
      USBD_CUSTOMHID_OUTREPORT_BUF_SIZE);

  return (uint8_t)USBD_OK;
}

uint8_t USBD_CUSTOM_HID_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid;

  if (pdev->pClassData_HID_Custom == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData_HID_Custom;
  (void)USBD_LL_PrepareReceive(pdev, CUSTOM_HID_OUT_EP, hhid->Report_buf, USBD_CUSTOMHID_OUTREPORT_BUF_SIZE);
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_CUSTOM_HID_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_CUSTOM_HID_HandleTypeDef *hhid = (USBD_CUSTOM_HID_HandleTypeDef *)pdev->pClassData_HID_Custom;

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (hhid->IsReportAvailable == 1U)
  {
    ((USBD_CUSTOM_HID_ItfTypeDef *)pdev->pUserData_HID_Custom)->OutEvent(hhid->Report_buf[0], hhid->Report_buf[1]);
    hhid->IsReportAvailable = 0U;
  }

  return (uint8_t)USBD_OK;
}

static uint8_t *USBD_CUSTOM_HID_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_CUSTOM_HID_DeviceQualifierDesc);
  return USBD_CUSTOM_HID_DeviceQualifierDesc;
}

uint8_t USBD_CUSTOM_HID_RegisterInterface(USBD_HandleTypeDef *pdev, USBD_CUSTOM_HID_ItfTypeDef *fops)
{
  if (fops == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  pdev->pUserData_HID_Custom = fops;
  return (uint8_t)USBD_OK;
}

void USBD_Update_HID_Custom_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep, uint8_t out_ep, uint8_t str_idx)
{
  desc[11] = itf_no;
  desc[17] = str_idx;
  desc[29] = in_ep;
  desc[36] = out_ep;

  CUSTOM_HID_IN_EP = in_ep;
  CUSTOM_HID_OUT_EP = out_ep;
  CUSTOM_HID_ITF_NBR = itf_no;
  CUSTOM_HID_STR_DESC_IDX = str_idx;
}
