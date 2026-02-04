#include "usbh_midi.h"

// TODO: a bunch of this is no longer called
// clean it up & document how the interface needs to be implemented to work.

static USBH_StatusTypeDef Init(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef DeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef ClassRequest(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef SOFProcess(USBH_HandleTypeDef *phost);

static void MIDI_ProcessTransmission(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi);

// Core class for standalone interface
USBH_ClassTypeDef MIDI_Class = {
  "MIDI",
  USB_AUDIO_CLASS,
  NULL, // MatchInterface
  Init,
  DeInit,
  ClassRequest,
  Process,
  SOFProcess, // SOFProcess
  NULL,
};

static USBH_StatusTypeDef SubInit(USBH_HandleTypeDef* phost, uint8_t itf_midi, void** hmidi);
static USBH_StatusTypeDef SubDeInit(USBH_HandleTypeDef* phost, void* hmidi);
static USBH_StatusTypeDef SubProcess(USBH_HandleTypeDef* phost, void* hmidi);

// SubDriver for inclusion in Composite interface
const USBH_MIDI_SubDriverTypeDef USBH_MIDI_SubDriver = {
  .Init = SubInit,
  .DeInit = SubDeInit,
  .Process = SubProcess,
};

static uint8_t in_pipe_number = 0xff;
static USBH_HandleTypeDef* _phost_handle = NULL;
static MIDI_HandleTypeDef* _hmidi = NULL;

// shared by standalone & subdriver
static void _Init(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint8_t interface){
  // stored global for URB handler
  _phost_handle = phost;
  _hmidi = hmidi;

  /*Collect the notification endpoint address and length*/
  // note we have 2 possible endpoints for a sender/receiver pair
  for(int i=0; i<2; i++){
    if(phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].bEndpointAddress & 0x80U){
      hmidi->InEp = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].bEndpointAddress);
      hmidi->InEpSize = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].wMaxPacketSize);
    } else {
      hmidi->OutEp = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].bEndpointAddress);
      hmidi->OutEpSize = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].wMaxPacketSize);
    }
  }

  /*Allocate the length for host channel number in*/
  hmidi->OutPipe = USBH_AllocPipe(phost, hmidi->OutEp);
  hmidi->InPipe = USBH_AllocPipe(phost, hmidi->InEp);

  /* Open pipe for Notification endpoint */
  (void)USBH_OpenPipe(phost, hmidi->OutPipe, hmidi->OutEp,
                      phost->device.address, phost->device.speed, USB_EP_TYPE_BULK,
                      hmidi->OutEpSize);
  (void)USBH_OpenPipe(phost, hmidi->InPipe, hmidi->InEp,
                      phost->device.address, phost->device.speed, USB_EP_TYPE_BULK,
                      hmidi->InEpSize);

  hmidi->state = HMIDI_IDLE_STATE;

  (void)USBH_LL_SetToggle(phost, hmidi->InPipe, 0U);
  (void)USBH_LL_SetToggle(phost, hmidi->OutPipe, 0U);

  in_pipe_number = hmidi->InPipe;
}

static USBH_StatusTypeDef SubInit(USBH_HandleTypeDef* phost, uint8_t interface, void** phmidi){
  *phmidi = (MIDI_HandleTypeDef*)USBH_malloc(sizeof(MIDI_HandleTypeDef));
  if(*phmidi == NULL){
    USBH_DbgLog("Cannot allocate memory for MIDI Handle");
    return USBH_FAIL;
  }
  (void)USBH_memset(*phmidi, 0, sizeof(MIDI_HandleTypeDef));
  _Init(phost, *phmidi, interface);
  return USBH_OK;
}

static USBH_StatusTypeDef Init(USBH_HandleTypeDef *phost){
  uint8_t interface = USBH_FindInterface(phost, USB_AUDIO_CLASS, USB_MIDISTREAMING_SubCLASS, 0xFF);
  if(interface == 0xFFU){ /* No Valid Interface */
    USBH_DbgLog("Cannot Find the interface for MIDI Class. %s", phost->pActiveClass->Name);
    return USBH_FAIL;
  }
  if(USBH_SelectInterface(phost, interface) != USBH_OK) return USBH_FAIL;

  MIDI_HandleTypeDef* hmidi = (MIDI_HandleTypeDef *)USBH_malloc(sizeof(MIDI_HandleTypeDef));
  if(!hmidi){
    USBH_DbgLog("Cannot allocate memory for MIDI Handle");
    return USBH_FAIL;
  }
  (void)USBH_memset(hmidi, 0, sizeof(MIDI_HandleTypeDef));
  phost->pActiveClass->pData = (void*)hmidi;

  _Init(phost, hmidi, interface);
  return USBH_OK;
};

static void _DeInit(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi){
  if(hmidi->OutPipe){
    (void)USBH_ClosePipe(phost, hmidi->OutPipe);
    (void)USBH_FreePipe(phost, hmidi->OutPipe);
    hmidi->OutPipe = 0U;    /* Reset the Channel as Free */
  }
  if(hmidi->InPipe){
    (void)USBH_ClosePipe(phost, hmidi->InPipe);
    (void)USBH_FreePipe(phost, hmidi->InPipe);
    hmidi->InPipe = 0U;     /* Reset the Channel as Free */
  }
  // invalidate global handles as device is not accessible [ie. hard fault]
  _phost_handle = NULL;
  _hmidi = NULL;
}

static USBH_StatusTypeDef SubDeInit(USBH_HandleTypeDef* phost, void* hmidi){
  _DeInit(phost, hmidi);
  if(hmidi){
    USBH_free(hmidi);
  }
  return USBH_OK;
}

static USBH_StatusTypeDef DeInit(USBH_HandleTypeDef *phost){
  MIDI_HandleTypeDef* hmidi = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;

  _DeInit(phost, hmidi);

  if(phost->pActiveClass->pData){
    USBH_free(phost->pActiveClass->pData);
    phost->pActiveClass->pData = 0U;
  }
  return USBH_OK;
}

static USBH_StatusTypeDef ClassRequest(USBH_HandleTypeDef *phost){
  phost->pUser(phost, HOST_USER_CLASS_ACTIVE);
  return USBH_OK;
}

static USBH_StatusTypeDef SOFProcess(USBH_HandleTypeDef *phost){
  UNUSED(phost);  
  return USBH_OK;
}

USBH_StatusTypeDef USBH_MIDI_Stop(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi){
  if (phost->gState == HOST_CLASS){
    hmidi->state = HMIDI_IDLE_STATE;
    (void)USBH_ClosePipe(phost, hmidi->InPipe);
    (void)USBH_ClosePipe(phost, hmidi->OutPipe);
  }
  return USBH_OK;
}

static USBH_StatusTypeDef _Process(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi){
  USBH_StatusTypeDef status = USBH_BUSY;
  switch (hmidi->state){
    case HMIDI_IDLE_STATE:
      status = USBH_OK;
      break;

    case HMIDI_TRANSFER_DATA:
      MIDI_ProcessTransmission(phost, hmidi);
      break;

    case HMIDI_ERROR_STATE:
      if(USBH_ClrFeature(phost, 0x00U) == USBH_OK){
        hmidi->state = HMIDI_IDLE_STATE;
      }
      break;

    default:
      break;
  }
  return status;
}

static USBH_StatusTypeDef SubProcess(USBH_HandleTypeDef* phost, void* hmidi){
  return _Process(phost, hmidi);
}

static USBH_StatusTypeDef Process(USBH_HandleTypeDef *phost){
  MIDI_HandleTypeDef* hmidi = (MIDI_HandleTypeDef*)phost->pActiveClass->pData;
  return _Process(phost, hmidi);
}

uint16_t USBH_MIDI_GetLastReceivedDataSize(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi){
  uint32_t dataSize = 0U;
  if (phost->gState == HOST_CLASS){
    dataSize = USBH_LL_GetLastXferSize(phost, hmidi->InPipe);
  }
  return (uint16_t)dataSize;
}

USBH_StatusTypeDef USBH_MIDI_Transmit(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint8_t *pbuff, uint32_t length){
  USBH_StatusTypeDef Status = USBH_BUSY;
  if ((hmidi->state == HMIDI_IDLE_STATE) || (hmidi->state == HMIDI_TRANSFER_DATA)){
    hmidi->pTxData = pbuff;
    hmidi->TxDataLength = length;
    hmidi->state = HMIDI_TRANSFER_DATA;
    hmidi->data_tx_state = HMIDI_SEND_DATA;
    Status = USBH_OK;
  }
  return Status;
}

static uint8_t* _rx_buffer = NULL;
static uint32_t _rx_buf_len = 0;
void USBH_MIDI_StartReception(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint8_t* pbuff, uint32_t length){
  _rx_buffer = pbuff;
  _rx_buf_len = length;
  // submit next URB
  hmidi->pRxData = _rx_buffer;
  hmidi->RxDataLength = _rx_buf_len;
  hmidi->state = HMIDI_TRANSFER_DATA;
  hmidi->data_rx_state = HMIDI_RECEIVE_DATA;
  (void)USBH_BulkReceiveData(phost,
                             hmidi->pRxData,
                             hmidi->InEpSize, // should be able to submit ->RxDataLength
                             hmidi->InPipe);
  hmidi->data_rx_state = HMIDI_RECEIVE_DATA_WAIT;
}

void USBH_MIDI_Retry(USBH_HandleTypeDef* phost, MIDI_HandleTypeDef* hmidi){
  hmidi->pRxData = _rx_buffer;
  hmidi->RxDataLength = _rx_buf_len;
  hmidi->state = HMIDI_TRANSFER_DATA;
  hmidi->data_rx_state = HMIDI_RECEIVE_DATA;
  (void)USBH_BulkReceiveData(phost,
                             hmidi->pRxData,
                             hmidi->InEpSize, // should be able to submit ->RxDataLength
                             hmidi->InPipe);
  hmidi->data_rx_state = HMIDI_RECEIVE_DATA_WAIT;
}


USBH_StatusTypeDef USBH_MIDI_Receive(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint8_t *pbuff, uint32_t length){
  USBH_StatusTypeDef Status = USBH_BUSY;
  if ((hmidi->state == HMIDI_IDLE_STATE) || (hmidi->state == HMIDI_TRANSFER_DATA)){
    hmidi->pRxData = pbuff; // location of destination buffer
    hmidi->RxDataLength = length; // size of destination buffer (not length of data received)
    hmidi->state = HMIDI_TRANSFER_DATA;
    hmidi->data_rx_state = HMIDI_RECEIVE_DATA;
    Status = USBH_OK;
  }
  return Status;
}

static void MIDI_ProcessTransmission(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi){
  USBH_URBStateTypeDef URB_Status = USBH_URB_IDLE;
  switch (hmidi->data_tx_state){
    case HMIDI_SEND_DATA:
      if (hmidi->TxDataLength > hmidi->OutEpSize){
        (void)USBH_BulkSendData(phost,
                                hmidi->pTxData,
                                hmidi->OutEpSize,
                                hmidi->OutPipe,
                                1U);
      } else {
        (void)USBH_BulkSendData(phost,
                                hmidi->pTxData,
                                (uint16_t)hmidi->TxDataLength,
                                hmidi->OutPipe,
                                1U);
      }
      hmidi->data_tx_state = HMIDI_SEND_DATA_WAIT;
      break;

    case HMIDI_SEND_DATA_WAIT:
      URB_Status = USBH_LL_GetURBState(phost, hmidi->OutPipe);
      /* Check the status done for transmission */
      if (URB_Status == USBH_URB_DONE){
        if (hmidi->TxDataLength > hmidi->OutEpSize){
          hmidi->TxDataLength -= hmidi->OutEpSize;
          hmidi->pTxData += hmidi->OutEpSize;
        } else {
          hmidi->TxDataLength = 0U;
        }

        if (hmidi->TxDataLength > 0U){
          hmidi->data_tx_state = HMIDI_SEND_DATA;
        } else {
          hmidi->data_tx_state = HMIDI_IDLE;
          USBH_MIDI_TransmitCallback(phost, hmidi);
        }
      } else if (URB_Status == USBH_URB_NOTREADY){
        hmidi->data_tx_state = HMIDI_SEND_DATA;
      }
      break;

    default:
      break;
  }
}

static void URB_Done(USBH_HandleTypeDef* phost, MIDI_HandleTypeDef* hmidi, uint32_t length){
  if(length > 0){ // increment pointers & immediately resubmit URB to get remaining data
    hmidi->RxDataLength -= length;
    hmidi->pRxData += length;
    hmidi->state = HMIDI_TRANSFER_DATA;
    hmidi->data_rx_state = HMIDI_RECEIVE_DATA;
    (void)USBH_BulkReceiveData(phost,
                               hmidi->pRxData,
                               hmidi->InEpSize, // should be able to submit ->RxDataLength
                               hmidi->InPipe);
    hmidi->data_rx_state = HMIDI_RECEIVE_DATA_WAIT;
  } else {
    hmidi->data_rx_state = HMIDI_IDLE;
    int total_length = _rx_buf_len - hmidi->RxDataLength;
    USBH_MIDI_ReceiveCallback(phost, hmidi, total_length);
    // DONT submit URB immediately
    // instead wait for 1ms timer to resubmit
  }
}

void USBH_MIDI_URBDoneCallback(int chnum, int xfer_count){
  if(chnum == in_pipe_number){
    if(_phost_handle && _hmidi){ // ensure driver is still active & valid
      URB_Done(_phost_handle, _hmidi, xfer_count);
    }
  }
}

__weak void USBH_MIDI_TransmitCallback(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi){
  UNUSED(phost);
  UNUSED(hmidi);
}

__weak void USBH_MIDI_ReceiveCallback(USBH_HandleTypeDef *phost, MIDI_HandleTypeDef* hmidi, uint32_t length){
  UNUSED(phost);
  UNUSED(hmidi);
}
