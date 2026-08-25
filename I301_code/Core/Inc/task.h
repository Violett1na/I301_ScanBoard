#ifndef __TASK_H__
#define __TASK_H__

#include "main.h"
#include "adc.h"
#include "dac.h"
#include "tim.h"
#include "dac.h"
#include "opamp.h"
#include "ad5290.h"
#include "gpio.h"
#include "ls_proto_device_app.h"

#define DAC_INX_SET(val)    (hdac1.Instance->DHR12R1 = (val))
#define DAC_FBX_SET(val)    (hdac1.Instance->DHR12R2 = (val))
#define DAC_INY_SET(val)    (hdac4.Instance->DHR12R1 = (val))
#define DAC_FBY_SET(val)    (hdac4.Instance->DHR12R2 = (val))

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


typedef struct
{
    uint8_t x1;
    uint8_t x2;
    uint8_t x3;
    uint8_t y1;
    uint8_t y2;
    uint8_t y3;
} radc_value_t;

typedef struct
{
    int16_t x;
    int16_t y;
} comp_value_t;

/* Flash 持久化存储总结构体，所有需要保存到 Flash 的参数统一放入此处 */
typedef struct
{
    radc_value_t  radc;           /* 电位器码值 */
    comp_value_t  comp;           /* 补偿值 */
    
} flash_store_t;



#pragma pack()



extern volatile adc_value_t adc_value;

extern radc_value_t radc_value;
extern comp_value_t comp_value;
extern flash_store_t flash_store;

void AD_DA_Init(void);
void ad5290_set_init(void);
void param_init(void);
void lsnet_init(void);

uint16_t adc_filter(uint16_t value);

/* --------------------------------------------------------------
 * AD-DA 处理通路（1MHz 逐样本线性处理、预留算法槽）
 * spec: docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md
 * -------------------------------------------------------------- */
#define AD_DA_BLOCK   16U                       /* RX/TX 乒乓缓冲每通道样本数 */
#define AD_DA_HALF    (AD_DA_BLOCK / 2U)        /* 半块长度 = 块处理单位 */
#define AD_DA_CH_NUM  4U                        /* 通道数: 0=vx 1=ix 2=vy 3=iy */

/* 算法槽签名：一次处理 AD_DA_CH_NUM 个通道各 n 个样本
 * in: 各通道 RX 半块指针数组；out: 各通道 TX 半块指针数组 */
typedef void (*ad_da_process_fn_t)(const uint16_t *in[AD_DA_CH_NUM],
                                   uint16_t       *out[AD_DA_CH_NUM],
                                   uint16_t        n);

extern ad_da_process_fn_t ad_da_process_fn;     /* 算法槽，默认指向 ad_da_process_linear */

/* --------------------------------------------------------------
 * MSB major-carry 对照测试（2026-08-17）
 * 假设: 静息码点落在 2047/2048 边界, 噪声使码值跨界, DAC major-carry
 *   (12 位同时翻转) 毛刺造成观测跳变。
 * 方法: 被测轴 DAC 改触发 NONE、TIM6 ≈30kHz 强驱乒乓序列(写即锁入),
 *   与 1µs 采样网格非整倍数 → 相位游走等效扫描边沿后 settling;
 *   管线 ADC 保持 1MHz 观测, 串口轮转 A/B/C 组并输出窗口统计+电流快照;
 *   示波器看 DAC 引脚(X=R101 左端/PA4)对比各组毛刺能量。
 * 组别: sel0=A 2047/2048(major-carry) sel1=B 2046/2047 sel2=C 2048/2049(单比特)
 * 拓扑注: DAC 输出不回 ADC-V 采样点(JP3 断直通), 观测侧必须在输出侧。
 * -------------------------------------------------------------- */
#define MSB_TEST_ENABLE 0U
#define MSB_TEST_AXIS_X 0U
#define MSB_TEST_AXIS_Y 0U
#define MSB_TEST_AXIS   MSB_TEST_AXIS_X   /* 探头位置: R101 左端(X) */
#define MSB_SNAP_LEN    128U              /* 电流采样快照长度 */
#define MSB_OUTLIER_TH  8U                /* 离群判定阈值(LSB) */
#define MSB_SWITCH_MS   2000U             /* 对照组轮转周期(ms) */

void msb_test_poll(void);

/* 默认线性算法（IN/FB 通道分制，饱和钳位 0..4095）：
 * IN 通道 ch0/ch2：y = (4095 - x) + off（抵消调理反相，spec §1）
 * FB 通道 ch1/ch3：y = x + off（静息 0V、有激励时出波形，spec §9.7） */
void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM],
                          uint16_t       *out[AD_DA_CH_NUM],
                          uint16_t        n);

/* 常量输出算法（断环诊断，2026-08-20）：冻结轴（task.c AD_DA_CONST_AXIS）
 * 忽略输入、IN 2048 中点 / FB 0；另一轴正常线性跟随保图像。
 * 冻结轴 ADC 保持采样、DAC 输出安静直流，用于判定噪声是否经
 * ADC→DAC 通路回灌（冻结轴通道抖动消失 = 该轴回灌环实锤）。 */
void ad_da_process_const(const uint16_t *in[AD_DA_CH_NUM],
                         uint16_t       *out[AD_DA_CH_NUM],
                         uint16_t        n);

#endif
