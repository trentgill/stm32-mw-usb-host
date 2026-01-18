#include "usbh_midi.h"

static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_SOFProcess(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost);
static void MIDI_ProcessTransmission(USBH_HandleTypeDef *phost);
static void MIDI_ProcessReception(USBH_HandleTypeDef *phost);

USBH_ClassTypeDef MIDI_Class = {
  "MIDI",
  USB_AUDIO_CLASS,
  USBH_MIDI_InterfaceInit,
  USBH_MIDI_InterfaceDeInit,
  USBH_MIDI_ClassRequest,
  USBH_MIDI_Process,
  USBH_MIDI_SOFProcess,
  NULL,
};

static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost){
  USBH_StatusTypeDef status = USBH_FAIL;
  uint8_t interface = 0;
  MIDI_HandleTypeDef *MIDI_Handle;

  interface = USBH_FindInterface(phost, USB_AUDIO_CLASS, USB_MIDISTREAMING_SubCLASS, 0xFF);
  if(interface == 0xFFU){ /* No Valid Interface */
    USBH_DbgLog("Cannot Find the interface for MIDI Class. %s", phost->pActiveClass->Name);
    return USBH_FAIL;
  }

  status = USBH_SelectInterface(phost, interface);
  if (status != USBH_OK){ return USBH_FAIL; }

  phost->pActiveClass->pData = (MIDI_HandleTypeDef *)USBH_malloc(sizeof(MIDI_HandleTypeDef));
  MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;
  if (MIDI_Handle == NULL){
    USBH_DbgLog("Cannot allocate memory for MIDI Handle");
    return USBH_FAIL;
  }

  /* Initialize cdc handler */
  (void)USBH_memset(MIDI_Handle, 0, sizeof(MIDI_HandleTypeDef));

  /*Collect the notification endpoint address and length*/
  // note we have 2 possible endpoints for a sender/receiver pair
  for(int i=0; i<2; i++){
    if(phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].bEndpointAddress & 0x80U){
      MIDI_Handle->InEp = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].bEndpointAddress);
      MIDI_Handle->InEpSize = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].wMaxPacketSize);
    } else {
      MIDI_Handle->OutEp = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].bEndpointAddress);
      MIDI_Handle->OutEpSize = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[i].wMaxPacketSize);
    }
  }

  /*Allocate the length for host channel number in*/
  MIDI_Handle->OutPipe = USBH_AllocPipe(phost, MIDI_Handle->OutEp);
  MIDI_Handle->InPipe = USBH_AllocPipe(phost, MIDI_Handle->InEp);

  /* Open pipe for Notification endpoint */
  (void)USBH_OpenPipe(phost, MIDI_Handle->OutPipe, MIDI_Handle->OutEp,
                      phost->device.address, phost->device.speed, USB_EP_TYPE_BULK,
                      MIDI_Handle->OutEpSize);
  (void)USBH_OpenPipe(phost, MIDI_Handle->InPipe, MIDI_Handle->InEp,
                      phost->device.address, phost->device.speed, USB_EP_TYPE_BULK,
                      MIDI_Handle->InEpSize);

  MIDI_Handle->state = MIDI_IDLE_STATE;

  (void)USBH_LL_SetToggle(phost, MIDI_Handle->InPipe, 0U);
  (void)USBH_LL_SetToggle(phost, MIDI_Handle->OutPipe, 0U);

  return USBH_OK;
}

static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost){
  MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;

  if(MIDI_Handle->OutPipe){
    (void)USBH_ClosePipe(phost, MIDI_Handle->OutPipe);
    (void)USBH_FreePipe(phost, MIDI_Handle->OutPipe);
    MIDI_Handle->OutPipe = 0U;    /* Reset the Channel as Free */
  }

  if(MIDI_Handle->InPipe){
    (void)USBH_ClosePipe(phost, MIDI_Handle->InPipe);
    (void)USBH_FreePipe(phost, MIDI_Handle->InPipe);
    MIDI_Handle->InPipe = 0U;     /* Reset the Channel as Free */
  }

  if(phost->pActiveClass->pData){
    USBH_free(phost->pActiveClass->pData);
    phost->pActiveClass->pData = 0U;
  }

  return USBH_OK;
}

static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost){
  phost->pUser(phost, HOST_USER_CLASS_ACTIVE);
  return USBH_OK;
}

USBH_StatusTypeDef USBH_MIDI_Stop(USBH_HandleTypeDef *phost){
  MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;

  if (phost->gState == HOST_CLASS){
    MIDI_Handle->state = MIDI_IDLE_STATE;
    (void)USBH_ClosePipe(phost, MIDI_Handle->InPipe);
    (void)USBH_ClosePipe(phost, MIDI_Handle->OutPipe);
  }
  return USBH_OK;
}

static USBH_StatusTypeDef USBH_MIDI_Process(USBH_HandleTypeDef *phost){
  USBH_StatusTypeDef status = USBH_BUSY;
  USBH_StatusTypeDef req_status = USBH_OK;
  MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;

  switch (MIDI_Handle->state){
    case MIDI_IDLE_STATE:
      status = USBH_OK;
      break;

    case MIDI_TRANSFER_DATA:
      MIDI_ProcessTransmission(phost);
      MIDI_ProcessReception(phost);
      break;

    case MIDI_ERROR_STATE:
      req_status = USBH_ClrFeature(phost, 0x00U);

      if (req_status == USBH_OK){
        MIDI_Handle->state = MIDI_IDLE_STATE;
      }
      break;

    default:
      break;
  }
  return status;
}

static USBH_StatusTypeDef USBH_MIDI_SOFProcess(USBH_HandleTypeDef *phost){
  UNUSED(phost);
  return USBH_OK;
}

uint16_t USBH_MIDI_GetLastReceivedDataSize(USBH_HandleTypeDef *phost){
  uint32_t dataSize;
  MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;
  if (phost->gState == HOST_CLASS){
    dataSize = USBH_LL_GetLastXferSize(phost, MIDI_Handle->InPipe);
  } else {
    dataSize =  0U;
  }
  return (uint16_t)dataSize;
}

USBH_StatusTypeDef USBH_MIDI_Transmit(USBH_HandleTypeDef *phost, uint8_t *pbuff, uint32_t length){
  USBH_StatusTypeDef Status = USBH_BUSY;
  MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;
  if ((MIDI_Handle->state == MIDI_IDLE_STATE) || (MIDI_Handle->state == MIDI_TRANSFER_DATA)){
    MIDI_Handle->pTxData = pbuff;
    MIDI_Handle->TxDataLength = length;
    MIDI_Handle->state = MIDI_TRANSFER_DATA;
    MIDI_Handle->data_tx_state = MIDI_SEND_DATA;
    Status = USBH_OK;
  }
  return Status;
}

USBH_StatusTypeDef USBH_MIDI_Receive(USBH_HandleTypeDef *phost, uint8_t *pbuff, uint32_t length){
  USBH_StatusTypeDef Status = USBH_BUSY;
  MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;
  if ((MIDI_Handle->state == MIDI_IDLE_STATE) || (MIDI_Handle->state == MIDI_TRANSFER_DATA)){
    MIDI_Handle->pRxData = pbuff;
    MIDI_Handle->RxDataLength = length;
    MIDI_Handle->state = MIDI_TRANSFER_DATA;
    MIDI_Handle->data_rx_state = MIDI_RECEIVE_DATA;
    Status = USBH_OK;
  }
  return Status;
}

static void MIDI_ProcessTransmission(USBH_HandleTypeDef *phost){
  MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;
  USBH_URBStateTypeDef URB_Status = USBH_URB_IDLE;
  switch (MIDI_Handle->data_tx_state){
    case MIDI_SEND_DATA:
      if (MIDI_Handle->TxDataLength > MIDI_Handle->OutEpSize){
        (void)USBH_BulkSendData(phost,
                                MIDI_Handle->pTxData,
                                MIDI_Handle->OutEpSize,
                                MIDI_Handle->OutPipe,
                                1U);
      } else {
        (void)USBH_BulkSendData(phost,
                                MIDI_Handle->pTxData,
                                (uint16_t)MIDI_Handle->TxDataLength,
                                MIDI_Handle->OutPipe,
                                1U);
      }
      MIDI_Handle->data_tx_state = MIDI_SEND_DATA_WAIT;
      break;

    case MIDI_SEND_DATA_WAIT:
      URB_Status = USBH_LL_GetURBState(phost, MIDI_Handle->OutPipe);
      /* Check the status done for transmission */
      if (URB_Status == USBH_URB_DONE){
        if (MIDI_Handle->TxDataLength > MIDI_Handle->OutEpSize){
          MIDI_Handle->TxDataLength -= MIDI_Handle->OutEpSize;
          MIDI_Handle->pTxData += MIDI_Handle->OutEpSize;
        } else {
          MIDI_Handle->TxDataLength = 0U;
        }

        if (MIDI_Handle->TxDataLength > 0U){
          MIDI_Handle->data_tx_state = MIDI_SEND_DATA;
        } else {
          MIDI_Handle->data_tx_state = MIDI_IDLE;
          USBH_MIDI_TransmitCallback(phost);
        }
      } else if (URB_Status == USBH_URB_NOTREADY){
        MIDI_Handle->data_tx_state = MIDI_SEND_DATA;
      }
      break;

    default:
      break;
  }
}

static void MIDI_ProcessReception(USBH_HandleTypeDef *phost){
  MIDI_HandleTypeDef *MIDI_Handle = (MIDI_HandleTypeDef *) phost->pActiveClass->pData;
  USBH_URBStateTypeDef URB_Status = USBH_URB_IDLE;
  uint32_t length;
  switch (MIDI_Handle->data_rx_state){
    case MIDI_RECEIVE_DATA:
      (void)USBH_BulkReceiveData(phost,
                                 MIDI_Handle->pRxData,
                                 MIDI_Handle->InEpSize,
                                 MIDI_Handle->InPipe);
      MIDI_Handle->data_rx_state = MIDI_RECEIVE_DATA_WAIT;
      break;

    case MIDI_RECEIVE_DATA_WAIT:
      URB_Status = USBH_LL_GetURBState(phost, MIDI_Handle->InPipe);
      /*Check the status done for reception*/
      if (URB_Status == USBH_URB_DONE){
        length = USBH_LL_GetLastXferSize(phost, MIDI_Handle->InPipe);
        if (((MIDI_Handle->RxDataLength - length) > 0U) && (length > MIDI_Handle->InEpSize)){
          MIDI_Handle->RxDataLength -= length;
          MIDI_Handle->pRxData += length;
          MIDI_Handle->data_rx_state = MIDI_RECEIVE_DATA;
        } else {
          MIDI_Handle->data_rx_state = MIDI_IDLE;
          USBH_MIDI_ReceiveCallback(phost);
        }
      }
      break;

    default:
      break;
  }
}

__weak void USBH_MIDI_TransmitCallback(USBH_HandleTypeDef *phost){
  UNUSED(phost);
}

__weak void USBH_MIDI_ReceiveCallback(USBH_HandleTypeDef *phost){
  UNUSED(phost);
}
