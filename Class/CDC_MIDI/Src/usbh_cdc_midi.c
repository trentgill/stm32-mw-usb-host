#include "usbh_cdc_midi.h"

// these files expose the SubDriver interface
#include "usbh_cdc.h"
#include "usbh_midi.h"

static USBH_StatusTypeDef MatchInterface(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef Init(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef DeInit(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef Process(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef SOFProcess(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef ClassRequest(USBH_HandleTypeDef* phost);

USBH_ClassTypeDef CDC_MIDI_Class = {
  .Name         = "CDC+MIDI",
  .ClassCode    = USB_CLASS_PER_INTERFACE, // generic composite class
  .Match        = MatchInterface,
  .Init         = Init,
  .DeInit       = DeInit,
  .Requests     = ClassRequest,
  .BgndProcess  = Process,
  .SOFProcess   = SOFProcess,
  .pData        = NULL,
};

// class matching defines
#define USB_CLASS_CDC         0x02
#define USB_SUBCLASS_CDC_ACM  0x02
#define USB_CLASS_AUDIO       0x01
#define USB_SUBCLASS_MIDI     0x03
#define USB_CLASS_MISC        0xEF
#define USB_SUBCLASS_IAD      0x02
#define USB_CLASS_DATA        0x0A
#define USB_SUBCLASS_CDC_DATA 0x00

static USBH_StatusTypeDef MatchInterface(USBH_HandleTypeDef* phost){

  // FIXME support IAD (if found, ignore cdc & data searching)
  uint8_t cdc_ctrl = USBH_FindInterface(phost, USB_CLASS_CDC, USB_SUBCLASS_CDC_ACM, 0xFF);
  uint8_t cdc_data = USBH_FindInterface(phost, USB_CLASS_DATA, USB_SUBCLASS_CDC_DATA, 0x00); // use 0xFF to match *any*
  uint8_t has_midi = USBH_FindInterface(phost, USB_CLASS_AUDIO, USB_SUBCLASS_MIDI, 0xFF);

  // require both ctrl+data to satisfy CDC
  uint8_t has_cdc = (cdc_ctrl!=0xFF) && (cdc_data!=0xFF);

  return (has_cdc && (has_midi!=0xFF)) ? USBH_OK : USBH_FAIL;
}

static USBH_StatusTypeDef Init(USBH_HandleTypeDef* phost){
  uint8_t itf_cdc_ctrl = USBH_FindInterface(phost, USB_CLASS_CDC, USB_SUBCLASS_CDC_ACM, 0xFF);
  uint8_t itf_cdc_data = USBH_FindInterface(phost, USB_CLASS_DATA, USB_SUBCLASS_CDC_DATA, 0x00);
  uint8_t itf_midi = USBH_FindInterface(phost, USB_CLASS_AUDIO, USB_SUBCLASS_MIDI, 0xFF);
  if(itf_cdc_ctrl == 0xFF || itf_cdc_data == 0xFF || itf_midi == 0xFF){
    USBH_DbgLog("Cannot Find the interfaces for CDC+MIDI Class");
    return USBH_FAIL;
  }

  // Allocate the composite handle
  CDC_MIDI_HandleTypeDef* hCdcMidi = (CDC_MIDI_HandleTypeDef*)USBH_malloc(sizeof(CDC_MIDI_HandleTypeDef));
  if(!hCdcMidi){
    USBH_DbgLog("Cannot allocate memory for CDC+MIDI Handle");
    return USBH_FAIL;
  }
  phost->pActiveClass->pData = (void*)hCdcMidi;
  // clear memory
  (void)USBH_memset(hCdcMidi, 0, sizeof(CDC_MIDI_HandleTypeDef));

  // save our found interfaces
  hCdcMidi->itf_cdc_ctrl = itf_cdc_ctrl;
  hCdcMidi->itf_cdc_data = itf_cdc_data;
  hCdcMidi->itf_midi = itf_midi;
  printf("cdc+midi: interfaces 0x%x 0x%x 0x%x\n\r", (int)itf_cdc_ctrl, (int)itf_cdc_data, (int)itf_midi);

  // handle failure (must dealloc anything so far, and return FAIL)
  // SubDrivers will write their handles into the provided pointers
  if(USBH_CDC_SubDriver.Init(phost, hCdcMidi->itf_cdc_ctrl
                                  , hCdcMidi->itf_cdc_data
                                  , &(hCdcMidi->handle_cdc)) != USBH_OK){
    USBH_DbgLog("CDC+MIDI subdriver for CDC failed to Init");
    goto unroll_malloc;
  }
  if(USBH_MIDI_SubDriver.Init(phost, hCdcMidi->itf_midi
                                   , &(hCdcMidi->handle_midi)) != USBH_OK){
    USBH_DbgLog("CDC+MIDI subdriver for MIDI failed to Init");
    goto unroll_cdc;
  }

  return USBH_OK;

unroll_cdc:
  USBH_CDC_SubDriver.DeInit(phost, hCdcMidi->handle_cdc);

unroll_malloc:
  USBH_free(phost->pActiveClass->pData);
  phost->pActiveClass->pData = 0U;

  return USBH_FAIL;
}

static USBH_StatusTypeDef DeInit(USBH_HandleTypeDef *phost){
  if((phost->pActiveClass->pData) != NULL){
    CDC_MIDI_HandleTypeDef* hCdcMidi = (CDC_MIDI_HandleTypeDef*)phost->pActiveClass->pData;
    if(hCdcMidi->handle_midi){
      USBH_MIDI_SubDriver.DeInit(phost, hCdcMidi->handle_midi);
    }
    if(hCdcMidi->handle_cdc){
      USBH_CDC_SubDriver.DeInit(phost, hCdcMidi->handle_cdc);
    }
    USBH_free(phost->pActiveClass->pData);
    phost->pActiveClass->pData = 0U;
  }
  return USBH_OK;
}

static USBH_StatusTypeDef Process(USBH_HandleTypeDef* phost){
  CDC_MIDI_HandleTypeDef* hCdcMidi = (CDC_MIDI_HandleTypeDef*)phost->pActiveClass->pData;
  USBH_StatusTypeDef status = USBH_OK; // 0. only other option is busy (1)
  status |= USBH_CDC_SubDriver.Process(phost, hCdcMidi->handle_cdc);
  status |= USBH_MIDI_SubDriver.Process(phost, hCdcMidi->handle_midi);
  return status; // status is unused by the driver anyway
}

static USBH_StatusTypeDef SOFProcess(USBH_HandleTypeDef* phost){
  UNUSED(phost);
  return USBH_OK;
}

static USBH_StatusTypeDef ClassRequest(USBH_HandleTypeDef* phost){
  phost->pUser(phost, HOST_USER_CLASS_ACTIVE);
  return USBH_OK;
}
