#include "usbd_bulk.h"
#include "usbd_ctlreq.h"
#include "ls_proto_receive.h"



extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t USBD_BULK_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_BULK_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_BULK_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_BULK_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_BULK_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t *USBD_BULK_GetFSCfgDesc(uint16_t *length);
static void bulk_send_next_packet(void);

#define WINUSB_VENDOR_CODE             0x20U
#define WINUSB_MS_OS_20_DESC_INDEX     0x0007U

__ALIGN_BEGIN static const uint8_t USBD_WinUsbDescriptorSet[] __ALIGN_END =
{
    /* Microsoft OS 2.0 descriptor set header */
    0x0A, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x03, 0x06,
    0xB2, 0x00,

    /* Configuration subset header (configuration value 1) */
    0x08, 0x00,
    0x01, 0x00,
    0x01, 0x00,
    0xA8, 0x00,

    /* Function subset header (interface 0) */
    0x08, 0x00,
    0x02, 0x00,
    0x00, 0x00,
    0xA0, 0x00,

    /* Compatible ID descriptor */
    0x14, 0x00,
    0x03, 0x00,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Registry property descriptor: DeviceInterfaceGUIDs */
    0x84, 0x00,
    0x04, 0x00,
    0x07, 0x00,
    0x2A, 0x00,
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
    'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
    'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
    'D', 0x00, 's', 0x00, 0x00, 0x00,
    0x50, 0x00,
    '{', 0x00, 'D', 0x00, '3', 0x00, '5', 0x00, 'F', 0x00, '7', 0x00,
    '8', 0x00, '4', 0x00, 'C', 0x00, '-', 0x00, '6', 0x00, 'A', 0x00,
    '0', 0x00, 'E', 0x00, '-', 0x00, '4', 0x00, 'A', 0x00, '5', 0x00,
    'A', 0x00, '-', 0x00, '9', 0x00, '0', 0x00, 'F', 0x00, '5', 0x00,
    '-', 0x00, '0', 0x00, '9', 0x00, '1', 0x00, 'C', 0x00, '4', 0x00,
    'D', 0x00, '2', 0x00, 'F', 0x00, '1', 0x00, 'F', 0x00, '1', 0x00,
    'B', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00
};


static uint8_t  *bulk_tx_buf      = NULL;   // 指向当前发送数据,用于辅助分包发送，属于bulk内部使用
static uint32_t  bulk_tx_len      = 0;      // 总长度
static uint32_t  bulk_tx_offset   = 0;      // 已发送长度
volatile uint8_t bulk_tx_busy = 0;

uint8_t  bulk_trans_buf[BULK_RX_BUF_SIZE]; // 实际usb使用发送缓冲区
uint16_t bulk_trans_len = 0;              // 发送数据长度
uint8_t  bulk_rx_buf[BULK_RX_BUF_SIZE];
volatile uint16_t bulk_rx_len = 0;
uint8_t  bulk_rx_busy = 0;
uint8_t  bulk_rx_ready = 0;


USBD_ClassTypeDef USBD_BULK =
    {
        USBD_BULK_Init,
        USBD_BULK_DeInit,
        USBD_BULK_Setup,
        NULL,
        NULL,
        USBD_BULK_DataIn,
        USBD_BULK_DataOut,
        NULL,
        NULL,
        NULL,
#ifndef USE_USBD_COMPOSITE
        NULL,
        USBD_BULK_GetFSCfgDesc,
        NULL,
        NULL,
#endif
};

/* USB Bulk device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_BULK_CfgDesc[USB_BULK_CONFIG_DESC_SIZ] __ALIGN_END =
    {
        /* Configuration Descriptor */
        0x09,                           /* bLength */
        USB_DESC_TYPE_CONFIGURATION,    /* bDescriptorType */
        USB_BULK_CONFIG_DESC_SIZ, 0x00, /* wTotalLength */
        0x01,                           /* bNumInterfaces */
        0x01,                           /* bConfigurationValue */
        0x00,                           /* iConfiguration */
#if (USBD_SELF_POWERED == 1U)
        0xC0,
#else
        0x80,
#endif
        USBD_MAX_POWER,

        /* Interface Descriptor */
        0x09,                    /* bLength */
        USB_DESC_TYPE_INTERFACE, /* bDescriptorType */
        0x00,                    /* bInterfaceNumber */
        0x00,                    /* bAlternateSetting */
        0x02,                    /* bNumEndpoints */
        0xFF,                    /* bInterfaceClass : Vendor */
        0x00,                    /* bInterfaceSubClass */
        0x00,                    /* bInterfaceProtocol */
        USBD_IDX_INTERFACE_STR,  /* iInterface */

        /* Endpoint OUT Descriptor */
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        BULK_OUT_EP, /* EP1 OUT */
        0x02,        /* Bulk */
        LOBYTE(BULK_FS_MAX_PACKET_SIZE),
        HIBYTE(BULK_FS_MAX_PACKET_SIZE),
        0x00,

        /* Endpoint IN Descriptor */
        0x07,
        USB_DESC_TYPE_ENDPOINT,
        BULK_IN_EP, /* EP1 IN */
        0x02,       /* Bulk */
        LOBYTE(BULK_FS_MAX_PACKET_SIZE),
        HIBYTE(BULK_FS_MAX_PACKET_SIZE),
        0x00
    };

static uint8_t *USBD_BULK_GetFSCfgDesc(uint16_t *length)
{
    *length = sizeof(USBD_BULK_CfgDesc);
    return USBD_BULK_CfgDesc;
}

static uint8_t USBD_BULK_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);

    USBD_BULK_HandleTypeDef *hbulkc;

    hbulkc = USBD_malloc(sizeof(USBD_BULK_HandleTypeDef));
    if (hbulkc == NULL)
    {
        return USBD_FAIL;
    }

    pdev->pClassData = (void *)hbulkc;

    USBD_LL_OpenEP(pdev, BULK_IN_EP, USBD_EP_TYPE_BULK, BULK_FS_MAX_PACKET_SIZE);
    USBD_LL_OpenEP(pdev, BULK_OUT_EP, USBD_EP_TYPE_BULK, BULK_FS_MAX_PACKET_SIZE);

    USBD_LL_PrepareReceive(pdev,
                           BULK_OUT_EP,
                           hbulkc->RxBuffer,
                           BULK_FS_MAX_PACKET_SIZE);

    return USBD_OK;
}

static uint8_t USBD_BULK_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);

    USBD_LL_CloseEP(pdev, BULK_IN_EP);
    USBD_LL_CloseEP(pdev, BULK_OUT_EP);

    if (pdev->pClassData != NULL)
    {
        USBD_free(pdev->pClassData);
        pdev->pClassData = NULL;
    }

    return USBD_OK;
}

static uint8_t USBD_BULK_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    if (((req->bmRequest & 0x80U) != 0U) &&
        ((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_VENDOR) &&
        (req->bRequest == WINUSB_VENDOR_CODE) &&
        (req->wIndex == WINUSB_MS_OS_20_DESC_INDEX))
    {
        uint16_t len = MIN((uint16_t)sizeof(USBD_WinUsbDescriptorSet), req->wLength);
        USBD_CtlSendData(pdev, (uint8_t *)USBD_WinUsbDescriptorSet, len);
        return USBD_OK;
    }

    USBD_CtlError(pdev, req);
    return USBD_FAIL;
}



// 发送数据回调
static uint8_t USBD_BULK_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    if ((epnum | 0x80) == BULK_IN_EP)
    {
        bulk_send_next_packet();  
    }
    return USBD_OK;
}


static uint8_t USBD_BULK_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    if (epnum != (BULK_OUT_EP & 0x7F))
        return USBD_OK;

    USBD_BULK_HandleTypeDef *hbulkc = (USBD_BULK_HandleTypeDef *)pdev->pClassData;

    uint16_t len = USBD_LL_GetRxDataSize(pdev, epnum);

    if (bulk_rx_len + len > BULK_RX_BUF_SIZE){
        bulk_rx_len = 0;
        USBD_LL_PrepareReceive(pdev, BULK_OUT_EP, hbulkc->RxBuffer, BULK_FS_MAX_PACKET_SIZE);
        return USBD_FAIL;
    }
    memcpy(&bulk_rx_buf[bulk_rx_len], hbulkc->RxBuffer, len);
    bulk_rx_len += len;

    // LOG_SYS_INFO("usb bulk origin recv %d bytes", len);
    USBD_LL_PrepareReceive(
        pdev,
        BULK_OUT_EP,
        hbulkc->RxBuffer,
        BULK_FS_MAX_PACKET_SIZE
    );

    return USBD_OK;
}

static void bulk_send_next_packet(void)
{
    uint32_t remain = bulk_tx_len - bulk_tx_offset;
    uint16_t pkt_len;

    if (remain == 0)
    {
        if ((bulk_tx_len != 0) && (bulk_tx_len % BULK_MAX_PKT) == 0 && bulk_tx_busy == 1)
        {
            bulk_tx_busy = 2;  // 标记 ZLP 已发送，下次进入直接完成
            USBD_LL_Transmit(&hUsbDeviceFS, BULK_IN_EP, NULL, 0);
        }
        else
        {
            bulk_tx_busy = 0;
        }
        return;
    }

    pkt_len = (remain > BULK_MAX_PKT) ? BULK_MAX_PKT : remain;
    // LOG_SYS_INFO("usb bulk send %d bytes",pkt_len);
    USBD_LL_Transmit(&hUsbDeviceFS,
                     BULK_IN_EP,
                     bulk_tx_buf + bulk_tx_offset,
                     pkt_len);

    bulk_tx_offset += pkt_len;
}
// 发送大数据包（兼容0-大于64字节）
uint8_t USBD_BULK_SendLarge(uint8_t *buf, uint32_t len)
{
    if (bulk_tx_busy) return USBD_BUSY;

    bulk_tx_buf    = buf;
    bulk_tx_len    = len;
    bulk_tx_offset = 0;
    bulk_tx_busy   = 1;

    // 立刻启动第一包
    bulk_send_next_packet();

    return USBD_OK;
}

void  USBD_BULK_Recv(void)
{
    if (bulk_rx_len < LS_DATA_BASE_LEN) {
        return;
    }

    if (memcmp(bulk_rx_buf, LS_HEADER_STR, LS_HEADER_LEN) != 0)
    {
        LOG_SYS_ERROR("usb bulk recv head error.");
        bulk_rx_len = 0;
        return;
    }
    uint16_t length = bulk_rx_buf[13] << 8 | bulk_rx_buf[14];
    if (bulk_rx_len == length)
    {
        //处理数据
        LOG_SYS_INFO("usb bulk recv %d bytes", length);
        LOG_SYS_HEX("usb bulk recv data", bulk_rx_buf, length);
        ls_parse(&ls_device_pkt, bulk_rx_buf, length);
        //移除数据
        bulk_rx_len = 0;        
    }

}

