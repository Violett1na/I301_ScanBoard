/* port_trans.h —— PORT 契约: 字节流传输(现实现 = USB Bulk)
 * 语义: init 完成设备栈初始化; 接收按"累积缓冲 + peek/consume"交付,
 *   成帧判断(协议头/长度字段)属协议知识, 在 APP 层完成, 不进本层;
 *   发送为整包委托(忙时拒发, 与现状一致)。
 * 实现: Platform/<芯片>/port_trans_*.c。 */
#ifndef __PORT_TRANS_H__
#define __PORT_TRANS_H__

#include <stdint.h>

int port_trans_init(void);    /* 传输设备栈初始化: 0 成功 */

/* 发送: 0 成功, -1 忙/失败(调用方语义与现状一致: 忙即放弃本次) */
int port_trans_send(const uint8_t *buf, uint32_t len);

void port_trans_poll(void);   /* 主循环保守维护点(现实现为空操作) */

/* 接收: peek 返回当前累积字节数并给出缓冲指针(缓冲归平台所有,
 * 下次 consume 前有效); consume 后累积清零。 */
uint16_t port_trans_rx_peek(const uint8_t **buf);
void port_trans_rx_consume(uint16_t len);

#endif /* __PORT_TRANS_H__ */
