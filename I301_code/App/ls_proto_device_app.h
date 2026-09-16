/* ls_proto_device_app.h —— LIGHTSPACE-XY 协议设备侧胶水(APP 层)
 * 职责: 协议回调注册(电位器/补偿/保存/复位/设备信息/三套配置)、传输
 *   收发接线(发送经 port_trans, 接收成帧在 ls_app_poll)、强制套看门狗。
 * 2026-09-02 重构: 自 Proto/ 迁入 App/(协议核心保持硬件无关可复用于
 *   上位机, 设备侧胶水上移); 去 main.h/usbd 直连。
 * 2026-09-15: 新增三套配置回调与强制套超时自解除(spec §6.1)。 */
#ifndef LS_PROTO_DEVICE_APP_H
#define LS_PROTO_DEVICE_APP_H

#include <stdint.h>   /* uint32_t: 时基与超时时长(不依赖间接传递) */

#include "ls_proto_receive.h"
#include "ls_proto_trans.h"

#define SCANXY_VERSION 0x0001

void ls_app_init(void);   /* 注册协议回调(main 启动序列, 传输初始化之后) */
void ls_app_poll(void);   /* 主循环: 传输接收成帧 + 协议分发 */

/* 强制套超时时长(ms): 超过该时长未收到任何上位机整帧即自动解除
 * (spec §6.1)。暂定 5s, 待上板标定(spec §8 O3) */
#define LS_FORCE_TIMEOUT_MS   5000U

/* 强制套超时判定(纯函数, 无副作用, 不触硬件)
 * 抽为纯函数是为了让宿主单测能覆盖 uint32 毫秒回绕——项目规范 8-2
 * 明列"时间边界(uint32 毫秒回绕)"必测。无符号差值在回绕后仍等于
 * 真实间隔, 不会误判。
 * 输入: now_ms 当前时基; last_ms 最近一次收到上位机整帧的时基
 * 返回: 1 已超时, 0 未超时 */
static inline int ls_force_wd_expired(uint32_t now_ms, uint32_t last_ms)
{
    return ((uint32_t)(now_ms - last_ms) > LS_FORCE_TIMEOUT_MS) ? 1 : 0;
}

/* 强制套看门狗: 主循环周期调用; 已强制且超时则自动解除强制(spec §6.1) */
void ls_app_force_watchdog_poll(void);

#endif /* LS_PROTO_DEVICE_APP_H */
