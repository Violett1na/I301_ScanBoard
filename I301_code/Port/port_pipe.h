/* port_pipe.h —— PORT 契约: AD-DA 实时管线(仅 AD-DA 板型)
 * 语义: 平台按固定节拍以 DMA 搬运样本, 每半块经算法槽回调处理;
 *   契约不暴露 DMA/触发链等机制。通道数取自板型头(板上布线事实);
 *   块长为平台中断预算参数, 在平台实现内定义(勿在契约层固化)。
 * 挂载时序: 主侧先 ad_da_process_fn_set(默认算法)再 port_pipe_init();
 *   ocd/ocd_sig 的包装在 init 之后(信令模块对已启动通路有再配置需求)。
 * 槽位所有权: 平台实现私有持有, 单写者(主侧初始化期), ISR 每拍只读。 */
#ifndef __PORT_PIPE_H__
#define __PORT_PIPE_H__

#include <stdint.h>
#include "i301_ad_da.h"   /* BOARD_AD_DA_CH_NUM */

#define AD_DA_CH_NUM  BOARD_AD_DA_CH_NUM   /* 通道数: 0=vx 1=ix 2=vy 3=iy */

/* 监测快照: ISR 每块写一次, 外部经 port_pipe_snapshot() 只读 */
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

/* 算法槽签名：一次处理 AD_DA_CH_NUM 个通道各 n 个样本
 * in: 各通道 RX 半块指针数组；out: 各通道 TX 半块指针数组 */
typedef void (*ad_da_process_fn_t)(const uint16_t *in[AD_DA_CH_NUM],
                                   uint16_t       *out[AD_DA_CH_NUM],
                                   uint16_t        n);

int port_pipe_init(void);    /* 0 成功; 算法槽未设或硬件失败返回 -1,
                                调用方走致命错误路径 */
void ad_da_process_fn_set(ad_da_process_fn_t fn);
ad_da_process_fn_t ad_da_process_fn_get(void);
const volatile adc_value_t *port_pipe_snapshot(void);

#endif /* __PORT_PIPE_H__ */
