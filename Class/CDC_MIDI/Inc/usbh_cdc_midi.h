#pragma once

#include "usbh_core.h"

#define USB_CLASS_PER_INTERFACE 0x00U

extern USBH_ClassTypeDef      CDC_MIDI_Class;
#define USBH_CDC_MIDI_CLASS  &CDC_MIDI_Class

typedef struct{
  uint8_t itf_cdc_ctrl;
  uint8_t itf_cdc_data;
  uint8_t itf_midi;
  void* handle_cdc;
  void* handle_midi;
} CDC_MIDI_HandleTypeDef;
