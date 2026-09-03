#ifndef __USBD_BULK_H__
#define __USBD_BULK_H__


#include "main.h"
#include  "usbd_ioreq.h"

// 定义BULK端点(不可修改)
#define BULK_IN_EP                0x81U   /* EP1 IN */
#define BULK_OUT_EP               0x01U   /* EP1 OUT */
#define BULK_FS_MAX_PACKET_SIZE   64U
#define USB_BULK_CONFIG_DESC_SIZ  (9 + 9 + 7 + 7)
#define BULK_MAX_PKT  64
//////////////////////////////


#define BULK_RX_BUF_SIZE  (1024*5)
typedef struct
{
  uint8_t  RxBuffer[BULK_FS_MAX_PACKET_SIZE];
  uint8_t  TxBuffer[BULK_FS_MAX_PACKET_SIZE];
  uint32_t RxLength;
  uint32_t TxLength;
} USBD_BULK_HandleTypeDef;

extern USBD_ClassTypeDef USBD_BULK;
extern volatile uint8_t bulk_tx_busy;
extern uint8_t bulk_rx_ready;
extern volatile uint16_t bulk_rx_len;
extern uint8_t  bulk_rx_buf[BULK_RX_BUF_SIZE];
extern uint8_t  bulk_trans_buf[BULK_RX_BUF_SIZE]; // 实际usb使用发送缓冲区
extern uint16_t bulk_trans_len;              // 发送数据长度

extern uint8_t usb_connect;

uint8_t USBD_BULK_SendLarge(uint8_t *buf, uint32_t len);

#endif