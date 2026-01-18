#pragma once

/* Includes ------------------------------------------------------------------*/
#include "usbh_hid.h"

// try by assuming the None is Mouse (may need to change for other devices)
// we *should* be able to determine type by reading the info report
typedef struct _HID_NONE_Info
{
  uint8_t x;
  uint8_t y;
  uint8_t buttons[3];
}
HID_NONE_Info_TypeDef;


#ifndef USBH_HID_NONE_REPORT_SIZE
#define USBH_HID_NONE_REPORT_SIZE                       0x8U
#endif /* USBH_HID_NONE_REPORT_SIZE */

USBH_StatusTypeDef USBH_HID_NoneInit(USBH_HandleTypeDef *phost);
HID_NONE_Info_TypeDef *USBH_HID_GetNoneInfo(USBH_HandleTypeDef *phost);
