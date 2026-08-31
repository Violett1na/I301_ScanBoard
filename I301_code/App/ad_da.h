/* ad_da.h —— AD-DA 实时管线接口(I301 振镜 XY 板)
 * 职责: 1MHz AD-DA 处理通路声明(块长/算法槽/线性算法)、
 *   监测快照类型(adc_value_t)。参数层见 param.h。
 * 上下游: ad_da.c 实现; main.c/ocd/ocd_sig 引用;
 *   管线节拍 = DMA HT/TC(ad_da.c)。 */
#ifndef __TASK_H__
#define __TASK_H__

#include "main.h"

#pragma pack(1)
typedef struct
{
    uint16_t x;
    uint16_t y;
} adc_value_fb_t;

typedef struct
{
    uint16_t vx;
    uint16_t vy;
    uint16_t ix;
    uint16_t iy;

    adc_value_fb_t fb;
} adc_value_t;
#pragma pack()

extern volatile adc_value_t adc_value;   /* 监测快照: ISR 每块写一次, 主循环/协议读 */

void ad_da_init(void);       /* AD-DA 通路初始化: 校准/DAC 重配/预填/TX-RX DMA/时钟释放时序, 见 ad_da.c */

/* --------------------------------------------------------------
 * AD-DA 处理通路（1MHz 逐样本线性处理、预留算法槽）
 * spec: docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md
 * -------------------------------------------------------------- */
/* ⚠️ 块长即中断预算: 每拍预算 = AD_DA_HALF × 1µs × 170MHz。
 * 16(8µs/1360周期)时全链(ocd_sig+ocd+线性, 实测≈1200-1400周期)贴线偏超,
 * 上电靠 ocd 跳闸 HOLD 的廉价路径侥幸存活, HOLD 结束(200ms)即整机冻结
 * (2026-08-28 根因, 详见记忆); 32(16µs/2720周期)占用率≈50%, 裕量充足。
 * 勿再改小; 如需更低延迟先重测全链周期开销。 */
#define AD_DA_BLOCK   32U                       /* RX/TX 乒乓缓冲每通道样本数 */
#define AD_DA_HALF    (AD_DA_BLOCK / 2U)        /* 半块长度 = 块处理单位 */
#define AD_DA_CH_NUM  4U                        /* 通道数: 0=vx 1=ix 2=vy 3=iy */

/* 算法槽签名：一次处理 AD_DA_CH_NUM 个通道各 n 个样本
 * in: 各通道 RX 半块指针数组；out: 各通道 TX 半块指针数组 */
typedef void (*ad_da_process_fn_t)(const uint16_t *in[AD_DA_CH_NUM],
                                   uint16_t       *out[AD_DA_CH_NUM],
                                   uint16_t        n);

/* 算法槽: 主侧 init 期一次赋值(ocd/ocd_sig 包装), ISR 每拍读;
 * volatile 显式跨界可见性(单对齐字写入原子)。 */
extern volatile ad_da_process_fn_t ad_da_process_fn;

/* 默认线性算法（IN/FB 通道分制，饱和钳位 0..4095）：
 * IN 通道 ch0/ch2：y = (4095 - x) + off（抵消调理反相，spec §1）
 * FB 通道 ch1/ch3：y = x + off（静息 0V、有激励时出波形，spec §9.7） */
void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM],
                          uint16_t       *out[AD_DA_CH_NUM],
                          uint16_t        n);

#endif
