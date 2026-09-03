/* ls_proto_device_app.h —— LIGHTSPACE-XY 协议设备侧胶水(APP 层)
 * 职责: 协议回调注册(电位器/补偿/保存/复位/设备信息)、传输收发接线
 *   (发送经 port_trans, 接收成帧在 ls_app_poll)。
 * 2026-09-02 重构: 自 Proto/ 迁入 App/(协议核心保持硬件无关可复用于
 *   上位机, 设备侧胶水上移); 去 main.h/usbd 直连。 */
#ifndef LS_PROTO_DEVICE_APP_H
#define LS_PROTO_DEVICE_APP_H

#include "ls_proto_receive.h"
#include "ls_proto_trans.h"

#define SCANXY_VERSION 0x0001

void ls_app_init(void);   /* 注册协议回调(main 启动序列, 传输初始化之后) */
void ls_app_poll(void);   /* 主循环: 传输接收成帧 + 协议分发 */

#endif /* LS_PROTO_DEVICE_APP_H */
