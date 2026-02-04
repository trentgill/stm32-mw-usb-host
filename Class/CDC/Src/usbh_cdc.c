#include "usbh_cdc.h"

#define USBH_CDC_BUFFER_SIZE                 1024

static USBH_StatusTypeDef Init(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef DeInit(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef Process(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef ClassRequest(USBH_HandleTypeDef* phost);
static USBH_StatusTypeDef SOFProcess(USBH_HandleTypeDef *phost);

static USBH_StatusTypeDef GetLineCoding(USBH_HandleTypeDef* phost,
                                        CDC_LineCodingTypeDef* linecoding);
static USBH_StatusTypeDef SetLineCoding(USBH_HandleTypeDef* phost,
                                        CDC_LineCodingTypeDef* linecoding);

static void CDC_ProcessTransmission(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc);
static void CDC_ProcessReception(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc);

USBH_ClassTypeDef CDC_Class = {
  "CDC",
  USB_CDC_CLASS,
  NULL, // MatchInterface
  Init,
  DeInit,
  ClassRequest,
  Process,
  SOFProcess, // SOFProcess
  NULL,
};

static USBH_StatusTypeDef SubInit(USBH_HandleTypeDef* phost, uint8_t itf_ctrl, uint8_t itf_data, void** hcdc);
static USBH_StatusTypeDef SubDeInit(USBH_HandleTypeDef* phost, void* hcdc);
static USBH_StatusTypeDef SubProcess(USBH_HandleTypeDef* phost, void* hcdc);

// SubDriver for inclusion in Composite interface
const USBH_CDC_SubDriverTypeDef USBH_CDC_SubDriver = {
  .Init = SubInit,
  .DeInit = SubDeInit,
  .Process = SubProcess,
};

static void _Init(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc
                                           , uint8_t itf_ctrl
                                           , uint8_t itf_data){
  // Collect the notification endpoint address and length
  if((phost->device.CfgDesc.Itf_Desc[itf_ctrl].Ep_Desc[0].bEndpointAddress & 0x80U) != 0U){
    hcdc->CommItf.NotifEp = phost->device.CfgDesc.Itf_Desc[itf_ctrl].Ep_Desc[0].bEndpointAddress;
    hcdc->CommItf.NotifEpSize  = phost->device.CfgDesc.Itf_Desc[itf_ctrl].Ep_Desc[0].wMaxPacketSize;
  }

  // Allocate the length for host channel number in
  hcdc->CommItf.NotifPipe = USBH_AllocPipe(phost, hcdc->CommItf.NotifEp);

  // Open pipe for Notification endpoint
  (void)USBH_OpenPipe(phost, hcdc->CommItf.NotifPipe, hcdc->CommItf.NotifEp,
                      phost->device.address, phost->device.speed, USB_EP_TYPE_INTR,
                      hcdc->CommItf.NotifEpSize);

  (void)USBH_LL_SetToggle(phost, hcdc->CommItf.NotifPipe, 0U);

  // Collect the class specific endpoint address and length
  if((phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[0].bEndpointAddress & 0x80U) != 0U){
    hcdc->DataItf.InEp = phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[0].bEndpointAddress;
    hcdc->DataItf.InEpSize  = phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[0].wMaxPacketSize;
  } else {
    hcdc->DataItf.OutEp = phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[0].bEndpointAddress;
    hcdc->DataItf.OutEpSize  = phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[0].wMaxPacketSize;
  }

  if((phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[1].bEndpointAddress & 0x80U) != 0U){
    hcdc->DataItf.InEp = phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[1].bEndpointAddress;
    hcdc->DataItf.InEpSize  = phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[1].wMaxPacketSize;
  } else {
    hcdc->DataItf.OutEp = phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[1].bEndpointAddress;
    hcdc->DataItf.OutEpSize = phost->device.CfgDesc.Itf_Desc[itf_data].Ep_Desc[1].wMaxPacketSize;
  }

  // Allocate the length for host channel number out
  hcdc->DataItf.OutPipe = USBH_AllocPipe(phost, hcdc->DataItf.OutEp);

  // Allocate the length for host channel number in
  hcdc->DataItf.InPipe = USBH_AllocPipe(phost, hcdc->DataItf.InEp);

  // Open channel for OUT endpoint
  (void)USBH_OpenPipe(phost, hcdc->DataItf.OutPipe, hcdc->DataItf.OutEp,
                      phost->device.address, phost->device.speed, USB_EP_TYPE_BULK,
                      hcdc->DataItf.OutEpSize);

  // Open channel for IN endpoint
  (void)USBH_OpenPipe(phost, hcdc->DataItf.InPipe, hcdc->DataItf.InEp,
                      phost->device.address, phost->device.speed, USB_EP_TYPE_BULK,
                      hcdc->DataItf.InEpSize);

  hcdc->state = CDC_IDLE_STATE;

  (void)USBH_LL_SetToggle(phost, hcdc->DataItf.OutPipe, 0U);
  (void)USBH_LL_SetToggle(phost, hcdc->DataItf.InPipe, 0U);
}

static USBH_StatusTypeDef SubInit(USBH_HandleTypeDef* phost, uint8_t itf_ctrl, uint8_t itf_data, void** phcdc){
  *phcdc = (CDC_HandleTypeDef*)USBH_malloc(sizeof(CDC_HandleTypeDef));
  if(*phcdc == NULL){
    USBH_DbgLog("Cannot allocate memory for CDC Handle");
    return USBH_FAIL;
  }
  (void)USBH_memset(*phcdc, 0, sizeof(CDC_HandleTypeDef));
  _Init(phost, *phcdc, itf_ctrl, itf_data);
  return USBH_OK;
}

static USBH_StatusTypeDef Init(USBH_HandleTypeDef* phost){

  uint8_t itf_ctrl = USBH_FindInterface(phost, COMMUNICATION_INTERFACE_CLASS_CODE,
                                   ABSTRACT_CONTROL_MODEL, 0xFF); // any protocol will do
                                   // ABSTRACT_CONTROL_MODEL, NO_CLASS_SPECIFIC_PROTOCOL_CODE);
                                   // ABSTRACT_CONTROL_MODEL, COMMON_AT_COMMAND);
  if((itf_ctrl == 0xFFU) || (itf_ctrl >= USBH_MAX_NUM_INTERFACES)){ // No Valid Interface
    USBH_DbgLog("Cannot Find the interface for Communication Interface Class.");
    return USBH_FAIL;
  }

  USBH_StatusTypeDef status = USBH_SelectInterface(phost, itf_ctrl);
  if(status != USBH_OK){
    return USBH_FAIL;
  }

  uint8_t itf_data = USBH_FindInterface(phost, DATA_INTERFACE_CLASS_CODE,
                                   RESERVED, NO_CLASS_SPECIFIC_PROTOCOL_CODE);
  if ((itf_data == 0xFFU) || (itf_data >= USBH_MAX_NUM_INTERFACES)){ // No Valid Interface
    USBH_DbgLog("Cannot Find the interface for Data Interface Class.");
    return USBH_FAIL;
  }

  CDC_HandleTypeDef* hcdc = (CDC_HandleTypeDef*)USBH_malloc(sizeof(CDC_HandleTypeDef));
  if(!hcdc){
    USBH_DbgLog("Cannot allocate memory for CDC Handle");
    return USBH_FAIL;
  }
  (void)USBH_memset(hcdc, 0, sizeof(CDC_HandleTypeDef));
  phost->pActiveClass->pData = hcdc;

  _Init(phost, hcdc, itf_ctrl, itf_data);

  return USBH_OK;
}

static void _DeInit(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  if((hcdc->CommItf.NotifPipe) != 0U){
    (void)USBH_ClosePipe(phost, hcdc->CommItf.NotifPipe);
    (void)USBH_FreePipe(phost, hcdc->CommItf.NotifPipe);
    hcdc->CommItf.NotifPipe = 0U;     /* Reset the Channel as Free */
  }

  if((hcdc->DataItf.InPipe) != 0U){
    (void)USBH_ClosePipe(phost, hcdc->DataItf.InPipe);
    (void)USBH_FreePipe(phost, hcdc->DataItf.InPipe);
    hcdc->DataItf.InPipe = 0U;     /* Reset the Channel as Free */
  }

  if((hcdc->DataItf.OutPipe) != 0U){
    (void)USBH_ClosePipe(phost, hcdc->DataItf.OutPipe);
    (void)USBH_FreePipe(phost, hcdc->DataItf.OutPipe);
    hcdc->DataItf.OutPipe = 0U;    /* Reset the Channel as Free */
  }
}

static USBH_StatusTypeDef SubDeInit(USBH_HandleTypeDef* phost, void* hcdc){
  _DeInit(phost, hcdc);
  if(hcdc){
    USBH_free(hcdc);
  }
  return USBH_OK;
}

static USBH_StatusTypeDef DeInit(USBH_HandleTypeDef* phost){
  CDC_HandleTypeDef* hcdc = (CDC_HandleTypeDef*)phost->pActiveClass->pData;
  _DeInit(phost, hcdc);
  if(hcdc != NULL){
    USBH_free(hcdc);
    phost->pActiveClass->pData = 0U;
  }
  return USBH_OK;
}

// FIXME: needs to be SubDriver capable (unless runtime requests work)
static USBH_StatusTypeDef ClassRequest(USBH_HandleTypeDef* phost){
  USBH_StatusTypeDef status;
  CDC_HandleTypeDef* hcdc = (CDC_HandleTypeDef*)phost->pActiveClass->pData;
  status = GetLineCoding(phost, &hcdc->LineCoding);
  if(status == USBH_OK){
    phost->pUser(phost, HOST_USER_CLASS_ACTIVE);
  } else if (status == USBH_NOT_SUPPORTED){
    USBH_ErrLog("Control error: CDC: Device Get Line Coding configuration failed");
  }
  return status;
}

static USBH_StatusTypeDef SOFProcess(USBH_HandleTypeDef *phost){
  UNUSED(phost);  
  return USBH_OK;
}

static USBH_StatusTypeDef _Process(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  USBH_StatusTypeDef status = USBH_BUSY;
  USBH_StatusTypeDef req_status = USBH_OK;

  switch (hcdc->state){

    case CDC_IDLE_STATE:
      status = USBH_OK;
      break;

    case CDC_SET_LINE_CODING_STATE:
      req_status = SetLineCoding(phost, hcdc->pUserLineCoding);

      if(req_status == USBH_OK){
        hcdc->state = CDC_GET_LAST_LINE_CODING_STATE;
      } else {
        if(req_status != USBH_BUSY){
          hcdc->state = CDC_ERROR_STATE;
        }
      }
      break;

    case CDC_GET_LAST_LINE_CODING_STATE:
      req_status = GetLineCoding(phost, &(hcdc->LineCoding));

      if(req_status == USBH_OK){
        hcdc->state = CDC_IDLE_STATE;
        if((hcdc->LineCoding.b.bCharFormat == hcdc->pUserLineCoding->b.bCharFormat) &&
           (hcdc->LineCoding.b.bDataBits == hcdc->pUserLineCoding->b.bDataBits) &&
           (hcdc->LineCoding.b.bParityType == hcdc->pUserLineCoding->b.bParityType) &&
           (hcdc->LineCoding.b.dwDTERate == hcdc->pUserLineCoding->b.dwDTERate)){
          USBH_CDC_LineCodingChanged(phost, hcdc);
        }
      } else {
        if(req_status != USBH_BUSY){
          hcdc->state = CDC_ERROR_STATE;
        }
      }
      break;

    case CDC_TRANSFER_DATA:
      CDC_ProcessTransmission(phost, hcdc);
      CDC_ProcessReception(phost, hcdc);
      break;

    case CDC_ERROR_STATE:
      if(USBH_ClrFeature(phost, 0x00U) == USBH_OK){
        hcdc->state = CDC_IDLE_STATE;
      }
      break;

    default:
      break;
  }
  return status;
}

static USBH_StatusTypeDef SubProcess(USBH_HandleTypeDef* phost, void* hcdc){
  return _Process(phost, hcdc);
}

static USBH_StatusTypeDef Process(USBH_HandleTypeDef* phost){
  CDC_HandleTypeDef* hcdc = (CDC_HandleTypeDef*)phost->pActiveClass->pData;
  return _Process(phost, hcdc);
}

USBH_StatusTypeDef USBH_CDC_Stop(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  if(phost->gState == HOST_CLASS){
    hcdc->state = CDC_IDLE_STATE;
    (void)USBH_ClosePipe(phost, hcdc->CommItf.NotifPipe);
    (void)USBH_ClosePipe(phost, hcdc->DataItf.InPipe);
    (void)USBH_ClosePipe(phost, hcdc->DataItf.OutPipe);
  }
  return USBH_OK;
}

// This request allows the host to find out the currently configured line coding.
static USBH_StatusTypeDef GetLineCoding(USBH_HandleTypeDef* phost, CDC_LineCodingTypeDef* linecoding){
  phost->Control.setup.b.bmRequestType = USB_D2H | USB_REQ_TYPE_CLASS | USB_REQ_RECIPIENT_INTERFACE;
  phost->Control.setup.b.bRequest = CDC_GET_LINE_CODING;
  phost->Control.setup.b.wValue.w = 0U;
  phost->Control.setup.b.wIndex.w = 0U;
  phost->Control.setup.b.wLength.w = LINE_CODING_STRUCTURE_SIZE;
  return USBH_CtlReq(phost, linecoding->Array, LINE_CODING_STRUCTURE_SIZE);
}

// This request allows the host to specify typical asynchronous
// line-character formatting properties
static USBH_StatusTypeDef SetLineCoding(USBH_HandleTypeDef* phost,
                                        CDC_LineCodingTypeDef* linecoding){
  phost->Control.setup.b.bmRequestType = USB_H2D | USB_REQ_TYPE_CLASS | USB_REQ_RECIPIENT_INTERFACE;
  phost->Control.setup.b.bRequest = CDC_SET_LINE_CODING;
  phost->Control.setup.b.wValue.w = 0U;
  phost->Control.setup.b.wIndex.w = 0U;
  phost->Control.setup.b.wLength.w = LINE_CODING_STRUCTURE_SIZE;
  return USBH_CtlReq(phost, linecoding->Array, LINE_CODING_STRUCTURE_SIZE);
}

// This function prepares the state before issuing the class specific commands
USBH_StatusTypeDef USBH_CDC_SetLineCoding(USBH_HandleTypeDef* phost,
                                          CDC_HandleTypeDef* hcdc,
                                          CDC_LineCodingTypeDef* linecoding){
  if(phost->gState == HOST_CLASS){
    hcdc->state = CDC_SET_LINE_CODING_STATE;
    hcdc->pUserLineCoding = linecoding;
#if (USBH_USE_OS == 1U)
    USBH_OS_PutMessage(phost, USBH_CLASS_EVENT, 0U, 0U);
#endif /* (USBH_USE_OS == 1U) */
  }
  return USBH_OK;
}

// This function prepares the state before issuing the class specific commands
USBH_StatusTypeDef USBH_CDC_GetLineCoding(USBH_HandleTypeDef* phost,
                                          CDC_HandleTypeDef* hcdc,
                                          CDC_LineCodingTypeDef* linecoding){
  if((phost->gState == HOST_CLASS) || (phost->gState == HOST_CLASS_REQUEST)){
    *linecoding = hcdc->LineCoding;
    return USBH_OK;
  } else {
    return USBH_FAIL;
  }
}

uint16_t USBH_CDC_GetLastReceivedDataSize(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  uint32_t dataSize;
  if(phost->gState == HOST_CLASS){
    dataSize = USBH_LL_GetLastXferSize(phost, hcdc->DataItf.InPipe);
  } else {
    dataSize =  0U;
  }
  return (uint16_t)dataSize;
}

USBH_StatusTypeDef USBH_CDC_Transmit(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc, uint8_t *pbuff, uint32_t length){
  USBH_StatusTypeDef Status = USBH_BUSY;
  if((hcdc->state == CDC_IDLE_STATE) || (hcdc->state == CDC_TRANSFER_DATA)){
    hcdc->pTxData = pbuff;
    hcdc->TxDataLength = length;
    hcdc->state = CDC_TRANSFER_DATA;
    hcdc->data_tx_state = CDC_SEND_DATA;
    Status = USBH_OK;
#if (USBH_USE_OS == 1U)
    USBH_OS_PutMessage(phost, USBH_CLASS_EVENT, 0U, 0U);
#endif /* (USBH_USE_OS == 1U) */
  }
  return Status;
}

USBH_StatusTypeDef USBH_CDC_Receive(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc, uint8_t *pbuff, uint32_t length){
  USBH_StatusTypeDef Status = USBH_BUSY;
  if((hcdc->state == CDC_IDLE_STATE) || (hcdc->state == CDC_TRANSFER_DATA)){
    hcdc->pRxData = pbuff;
    hcdc->RxDataLength = length;
    hcdc->state = CDC_TRANSFER_DATA;
    hcdc->data_rx_state = CDC_RECEIVE_DATA;
    Status = USBH_OK;
#if (USBH_USE_OS == 1U)
    USBH_OS_PutMessage(phost, USBH_CLASS_EVENT, 0U, 0U);
#endif /* (USBH_USE_OS == 1U) */
  }
  return Status;
}

static void CDC_ProcessTransmission(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  USBH_URBStateTypeDef URB_Status = USBH_URB_IDLE;
  switch(hcdc->data_tx_state){
    case CDC_SEND_DATA:
      if(hcdc->TxDataLength > hcdc->DataItf.OutEpSize){
        (void)USBH_BulkSendData(phost,
                                hcdc->pTxData,
                                hcdc->DataItf.OutEpSize,
                                hcdc->DataItf.OutPipe,
                                1U);
      } else {
        (void)USBH_BulkSendData(phost,
                                hcdc->pTxData,
                                (uint16_t)hcdc->TxDataLength,
                                hcdc->DataItf.OutPipe,
                                1U);
      }
      hcdc->data_tx_state = CDC_SEND_DATA_WAIT;
      break;

    case CDC_SEND_DATA_WAIT:
      URB_Status = USBH_LL_GetURBState(phost, hcdc->DataItf.OutPipe);

      /* Check the status done for transmission */
      if(URB_Status == USBH_URB_DONE){
        if(hcdc->TxDataLength > hcdc->DataItf.OutEpSize){
          hcdc->TxDataLength -= hcdc->DataItf.OutEpSize;
          hcdc->pTxData += hcdc->DataItf.OutEpSize;
        } else {
          hcdc->TxDataLength = 0U;
        }

        if(hcdc->TxDataLength > 0U){
          hcdc->data_tx_state = CDC_SEND_DATA;
        } else {
          hcdc->data_tx_state = CDC_IDLE;
          USBH_CDC_TransmitCallback(phost, hcdc);
        }
#if (USBH_USE_OS == 1U)
        USBH_OS_PutMessage(phost, USBH_CLASS_EVENT, 0U, 0U);
#endif /* (USBH_USE_OS == 1U) */
      } else {
        if(URB_Status == USBH_URB_NOTREADY){
          hcdc->data_tx_state = CDC_SEND_DATA;

#if (USBH_USE_OS == 1U)
          USBH_OS_PutMessage(phost, USBH_CLASS_EVENT, 0U, 0U);
#endif /* (USBH_USE_OS == 1U) */
        }
      }
      break;

    default:
      break;
  }
}

static void CDC_ProcessReception(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  USBH_URBStateTypeDef URB_Status = USBH_URB_IDLE;
  uint32_t length;

  switch (hcdc->data_rx_state){

    case CDC_RECEIVE_DATA:
      (void)USBH_BulkReceiveData(phost,
                                 hcdc->pRxData,
                                 hcdc->DataItf.InEpSize,
                                 hcdc->DataItf.InPipe);
#if defined (USBH_IN_NAK_PROCESS) && (USBH_IN_NAK_PROCESS == 1U)
      phost->NakTimer = phost->Timer;
#endif  /* defined (USBH_IN_NAK_PROCESS) && (USBH_IN_NAK_PROCESS == 1U) */
      hcdc->data_rx_state = CDC_RECEIVE_DATA_WAIT;
      break;

    case CDC_RECEIVE_DATA_WAIT:
      URB_Status = USBH_LL_GetURBState(phost, hcdc->DataItf.InPipe);
      /*Check the status done for reception*/
      if(URB_Status == USBH_URB_DONE){
        length = USBH_LL_GetLastXferSize(phost, hcdc->DataItf.InPipe);
        if(((hcdc->RxDataLength - length) > 0U) && (length == hcdc->DataItf.InEpSize)){
          hcdc->RxDataLength -= length;
          hcdc->pRxData += length;
          hcdc->data_rx_state = CDC_RECEIVE_DATA;
        } else {
          hcdc->data_rx_state = CDC_IDLE;
          USBH_CDC_ReceiveCallback(phost, hcdc);
        }
#if (USBH_USE_OS == 1U)
        USBH_OS_PutMessage(phost, USBH_CLASS_EVENT, 0U, 0U);
#endif /* (USBH_USE_OS == 1U) */
      }
#if defined (USBH_IN_NAK_PROCESS) && (USBH_IN_NAK_PROCESS == 1U)
      else if(URB_Status == USBH_URB_NAK_WAIT){
        hcdc->data_rx_state = CDC_RECEIVE_DATA_WAIT;
        if((phost->Timer - phost->NakTimer) > phost->NakTimeout){
          phost->NakTimer = phost->Timer;
          USBH_ActivatePipe(phost, hcdc->DataItf.InPipe);
        }
#if (USBH_USE_OS == 1U)
        USBH_OS_PutMessage(phost, USBH_CLASS_EVENT, 0U, 0U);
#endif /* (USBH_USE_OS == 1U) */
      }
#endif /* defined (USBH_IN_NAK_PROCESS) && (USBH_IN_NAK_PROCESS == 1U) */
      break;

    default:
      break;
  }
}

__weak void USBH_CDC_TransmitCallback(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  UNUSED(phost);
  UNUSED(hcdc);
}

__weak void USBH_CDC_ReceiveCallback(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  UNUSED(phost);
}

__weak void USBH_CDC_LineCodingChanged(USBH_HandleTypeDef* phost, CDC_HandleTypeDef* hcdc){
  UNUSED(phost);
  UNUSED(hcdc);
}
