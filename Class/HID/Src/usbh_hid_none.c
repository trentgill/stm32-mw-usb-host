#include "usbh_hid_none.h"
#include "usbh_hid_parser.h"


static USBH_StatusTypeDef USBH_HID_NoneDecode(USBH_HandleTypeDef *phost);


HID_NONE_Info_TypeDef   none_info;
uint8_t                 none_report_data[USBH_HID_NONE_REPORT_SIZE];
uint8_t                 none_rx_report_buf[USBH_HID_NONE_REPORT_SIZE];

/* Structures defining how to access items in a HID none report */
/* Access button 1 state. */
static const HID_Report_ItemTypedef prop_b1 =
{
  none_report_data, /*data*/
  1,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min value device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access button 2 state. */
static const HID_Report_ItemTypedef prop_b2 =
{
  none_report_data, /*data*/
  1,     /*size*/
  1,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min value device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access button 3 state. */
static const HID_Report_ItemTypedef prop_b3 =
{
  none_report_data, /*data*/
  1,     /*size*/
  2,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min vale device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access x coordinate change. */
static const HID_Report_ItemTypedef prop_x =
{
  none_report_data + 1U, /*data*/
  8,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  1,     /*signed?*/
  0,     /*min value read can return*/
  0xFFFF,/*max value read can return*/
  0,     /*min vale device can report*/
  0xFFFF,/*max value device can report*/
  1      /*resolution*/
};

/* Access y coordinate change. */
static const HID_Report_ItemTypedef prop_y =
{
  none_report_data + 2U, /*data*/
  8,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  1,     /*signed?*/
  0,     /*min value read can return*/
  0xFFFF,/*max value read can return*/
  0,     /*min vale device can report*/
  0xFFFF,/*max value device can report*/
  1      /*resolution*/
};

USBH_StatusTypeDef USBH_HID_NoneInit(USBH_HandleTypeDef* phost){
  uint32_t i;
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *)phost->pActiveClass->pData;

  none_info.x = 0U;
  none_info.y = 0U;
  none_info.buttons[0] = 0U;
  none_info.buttons[1] = 0U;
  none_info.buttons[2] = 0U;

  for (i = 0U; i < sizeof(none_report_data); i++){
    none_report_data[i] = 0U;
    none_rx_report_buf[i] = 0U;
  }

  if(HID_Handle->length > sizeof(none_report_data)){
    HID_Handle->length = (uint16_t)sizeof(none_report_data);
  }
  HID_Handle->pData = none_rx_report_buf;

  if ((HID_QUEUE_SIZE * sizeof(none_report_data)) > sizeof(phost->device.Data)){
    return USBH_FAIL;
  } else {
    USBH_HID_FifoInit(&HID_Handle->fifo, phost->device.Data, (uint16_t)(HID_QUEUE_SIZE * sizeof(none_report_data)));
  }

  return USBH_OK;
}

HID_NONE_Info_TypeDef *USBH_HID_GetNoneInfo(USBH_HandleTypeDef *phost){
  if(USBH_HID_NoneDecode(phost) == USBH_OK){
    return &none_info;
  } else {
    return NULL;
  }
}

static USBH_StatusTypeDef USBH_HID_NoneDecode(USBH_HandleTypeDef *phost){
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  if ((HID_Handle->length == 0U) || (HID_Handle->fifo.buf == NULL)){
    return USBH_FAIL;
  }

  // Fill report
  if(USBH_HID_FifoRead(&HID_Handle->fifo, &none_report_data, HID_Handle->length) == HID_Handle->length){
    // Decode report
    none_info.x = (uint8_t)HID_ReadItem((HID_Report_ItemTypedef *) &prop_x, 0U);
    none_info.y = (uint8_t)HID_ReadItem((HID_Report_ItemTypedef *) &prop_y, 0U);

    none_info.buttons[0] = (uint8_t)HID_ReadItem((HID_Report_ItemTypedef *) &prop_b1, 0U);
    none_info.buttons[1] = (uint8_t)HID_ReadItem((HID_Report_ItemTypedef *) &prop_b2, 0U);
    none_info.buttons[2] = (uint8_t)HID_ReadItem((HID_Report_ItemTypedef *) &prop_b3, 0U);

    return USBH_OK;
  }
  return   USBH_FAIL;
}

