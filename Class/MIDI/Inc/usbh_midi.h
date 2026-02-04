#pragma once

#include "usbh_core.h"

#define USB_MIDI_RX_BUFFER_SIZE 64
#define USB_MIDI_TX_BUFFER_SIZE 64
#define USB_MIDI_DATA_IN_SIZE 64
#define USB_MIDI_DATA_OUT_SIZE 64
#define USB_MIDI_DATA_OUT_EP 0x02
#define USB_MIDI_DATA_IN_EP 0x81
#define USB_AUDIO_CLASS 0x01
#define USB_MIDISTREAMING_SubCLASS 0x03
#define USBH_MIDI_DESC_SIZE 9
#define USB_MIDI_CLASS USB_AUDIO_CLASS
#define USBH_MIDI_CLASS &MIDI_Class

extern USBH_ClassTypeDef MIDI_Class;

typedef enum{
  NoteOff = 0x8,
  NoteOn = 0x9,
  PolyPressure = 0xa,
  CC = 0xb,
  ProgramChange = 0xc,
  Aftertouch = 0xd,
  PitchBend = 0xe
} midi_event_t;

typedef enum{
  Chn1,
  Chn2,
  Chn3,
  Chn4,
  Chn5,
  Chn6,
  Chn7,
  Chn8,
  Chn9,
  Chn10,
  Chn11,
  Chn12,
  Chn13,
  Chn14,
  Chn15,
  Chn16,
} midi_chn_t;

typedef union {
  struct {
    uint32_t ALL;
  };
  struct {
    uint8_t cin_cable;
    uint8_t evnt0;
    uint8_t evnt1;
    uint8_t evnt2;
  };
  struct {
    uint8_t type:4;
    uint8_t cable:4;
    uint8_t chn:4; // mios32_midi_chn_t
    uint8_t event:4; // mios32_midi_event_t
    uint8_t value1;
    uint8_t value2;
  };
  struct {
    uint8_t cin:4;
    uint8_t dummy1_cable:4;
    uint8_t dummy1_chn:4;
    uint8_t dummy1_event:4;
    uint8_t currentNote:8;
    uint8_t velocity:8;
  };
  struct {
    uint8_t dummy2_cin:4;
    uint8_t dummy2_cable:4;
    uint8_t dummy2_chn:4;
    uint8_t dummy2_event:4;
    uint8_t cc_number:8;
    uint8_t value:8;
  };
  struct {
    uint8_t dummy3_cin:4;
    uint8_t dummy3_cable:4;
    uint8_t dummy3_chn:4;
    uint8_t dummy3_event:4;
    uint8_t program_change:8;
    uint8_t dummy3:8;
  };
} midi_package_t;

typedef enum{
  HMIDI_IDLE=0,
  HMIDI_SEND_DATA,
  HMIDI_SEND_DATA_WAIT,
  HMIDI_RECEIVE_DATA,
  HMIDI_RECEIVE_DATA_WAIT,
} HMIDI_DataStateTypeDef;

typedef enum{
  HMIDI_IDLE_STATE=0,
  HMIDI_TRANSFER_DATA,
  HMIDI_ERROR_STATE,
} HMIDI_StateTypeDef;

typedef struct _MIDI_Process{
  HMIDI_StateTypeDef state;
  uint8_t InPipe;
  uint8_t OutPipe;
  uint8_t OutEp;
  uint8_t InEp;
  uint16_t OutEpSize;
  uint16_t InEpSize;

  uint8_t *pTxData;;
  uint8_t *pRxData;;
  uint16_t TxDataLength;
  uint16_t RxDataLength;
  HMIDI_DataStateTypeDef data_tx_state;
  HMIDI_DataStateTypeDef data_rx_state;
  uint8_t Rx_Poll;
} MIDI_HandleTypeDef;

USBH_StatusTypeDef USBH_MIDI_Transmit(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint8_t *pbuff, uint32_t length);
USBH_StatusTypeDef USBH_MIDI_Receive(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint8_t *pbuff, uint32_t length);
uint16_t USBH_MIDI_GetLastReceivedDataSize(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi);
USBH_StatusTypeDef USBH_MIDI_Stop(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi);
void USBH_MIDI_TransmitCallback(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi);
void USBH_MIDI_ReceiveCallback(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint32_t length);
void USBH_MIDI_StartReception(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint8_t* pbuff, uint32_t length);
void USBH_MIDI_Retry(USBH_HandleTypeDef* phost, MIDI_HandleTypeDef* hmidi);

void USBH_MIDI_URBDoneCallback(int chnum, int xfer_count);

// SubDriver interface (for Composite Host)

typedef struct{
  USBH_StatusTypeDef(*Init)(USBH_HandleTypeDef* phost, uint8_t itf_midi, void** hmidi);
  USBH_StatusTypeDef(*DeInit)(USBH_HandleTypeDef* phost, void* hmidi);
  USBH_StatusTypeDef(*Process)(USBH_HandleTypeDef* phost, void* hmidi);
} USBH_MIDI_SubDriverTypeDef;

extern const USBH_MIDI_SubDriverTypeDef USBH_MIDI_SubDriver;
