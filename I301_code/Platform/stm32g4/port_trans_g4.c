/* port_trans_g4.c —— STM32G4 实现: USB Bulk 传输
 * 包装 ST USBD 库(USB/): 初始化经 usb_device 生成入口; 发送经
 * USBD_BULK_SendLarge(忙即拒发, 与现状一致); 接收把累积缓冲经
 * peek/consume 交付 —— 成帧判断属协议知识, 在 APP 层完成, 本文件
 * 不引用协议层任何符号。 */
#include "port_trans.h"
#include "usb_device.h"
#include "usbd_bulk.h"

int port_trans_init(void)
{
    MX_USB_DEVICE_Init();
    return 0;
}

int port_trans_send(const uint8_t *buf, uint32_t len)
{
    if (USBD_BULK_SendLarge((uint8_t *)buf, len) != USBD_OK)
    {
        return -1;   /* 忙/失败: 调用方语义 = 放弃本次(与现状一致) */
    }
    return 0;
}

void port_trans_poll(void)
{
    /* USBD 栈由中断驱动, 主循环侧无周期维护需求, 保留空实现 */
}

uint16_t port_trans_rx_peek(const uint8_t **buf)
{
    if (buf != NULL)
    {
        *buf = bulk_rx_buf;
    }
    return bulk_rx_len;
}

void port_trans_rx_consume(uint16_t len)
{
    (void)len;   /* 现行语义: 成帧消费或坏帧丢弃均为整体清零 */
    bulk_rx_len = 0U;
}

/* file end */
