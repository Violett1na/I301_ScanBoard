/* stubs_ls_app_deps.h —— 桩控制面(仅供强制套看门狗释放用例)
 * 职责: 声明 stubs_ls_app_deps.c 提供的"可设值/可计数"入口, 使被测 TU
 *   在不触硬件的前提下被驱动到各分支。
 * 上下游: 定义见 stubs_ls_app_deps.c; 使用方
 *   test_force_release.c。共用本头可让签名不一致在编译期暴露。 */
#ifndef STUBS_LS_APP_DEPS_H
#define STUBS_LS_APP_DEPS_H

#include <stdint.h>

/* param 层: 桩内强制态与 param_force_clear 调用次数 */
void stub_set_forced(uint8_t idx);
int  stub_force_clear_count(void);

/* port_tick 契约: 桩内毫秒时基 */
void stub_set_tick(uint32_t ms);

/* port_trans 契约: 待交付的接收字节流与其 consume 次数 */
void stub_set_rx(const uint8_t *buf, uint16_t len);
int  stub_rx_consume_count(void);

/* 只满足链接的桩(见 stubs_ls_app_deps.c 第 2 节)被调用过几次。
 * 本用例应当恒为 0: 非 0 说明用例已走到未建模的胶水分支, 需重新审视。 */
int stub_offpath_calls(void);

#endif /* STUBS_LS_APP_DEPS_H */
