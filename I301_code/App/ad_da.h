/* ad_da.h —— AD-DA 处理通路算法接口(I301 振镜 XY 板)
 * 职责: 默认线性算法声明。管线机制(校准/重配/DMA/节拍)属 port_pipe
 *   契约, 实现见 Platform/<芯片>/; 参数层见 param.h。
 * 上下游: ad_da_alg.c 实现; main 在启动时将其装配进算法槽
 *   (ad_da_process_fn_set), ocd/ocd_sig 依次包装。
 * 分层说明(2026-09-02 重构): 管线实现下沉平台层, 监测快照类型与
 *   算法槽签名移入 Port/port_pipe.h; 原 __TASK_H__ 头保护为
 *   task→ad_da 改名遗留, 一并修正(规范 14-3)。 */
#ifndef __AD_DA_H__
#define __AD_DA_H__

#include <stdint.h>
#include "port_pipe.h"   /* AD_DA_CH_NUM / ad_da_process_fn_t / adc_value_t */

/* 默认线性算法（IN/FB 通道分制，饱和钳位 0..4095）：
 * IN 通道 ch0/ch2：y = (4095 - x) + off（抵消调理反相，spec §1）
 * FB 通道 ch1/ch3：y = x + off（静息 0V、有激励时出波形，spec §9.7）
 * off 取协议补偿值：ch0/ch1(X轴)用 comp.x，ch2/ch3(Y轴)用 comp.y */
void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM],
                          uint16_t       *out[AD_DA_CH_NUM],
                          uint16_t        n);

#endif /* __AD_DA_H__ */
