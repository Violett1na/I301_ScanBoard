/**
 * @file    ad5290.h
 * @brief   AD5290 数字电位器驱动（GPIO 模拟 SPI，6 路共片选）
 *
 * 硬件连接：
 *   X 轴 3 路 + Y 轴 3 路，共 6 个 AD5290。
 *   每路有独立的 SCL（CLK）与 SDA（MOSI），全部挂在 GPIOC。
 *   6 路共用一根 CS（PC6）：CS 拉低开始移位，CS 上升沿将 8 bit 数据
 *   一次性更新到 RDAC。
 *
 * 通道阻值：
 *   CH_1 / CH_2 = 10K，CH_3 = 100K（参见原理图）。
 *
 * 用法：
 *   1) MX_GPIO_Init() 已把所有 SCL/SDA/CS 配为推挽输出；
 *   2) 调用 AD5290_Init() 完成初始电平 + 6 路写入 0x80（中点）；
 *   3) 单通道：AD5290_SetCode() 或 AD5290_SetOhm()；
 *   4) 6 路并行：AD5290_SetAllCode() / AD5290_SetAllOhm()，
 *      只拉一次 CS，6 路 SDA 同步移位，速度最快、各通道相位一致。
 */

#ifndef __AD5290_H
#define __AD5290_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* 轴索引 */
typedef enum {
    AD5290_AXIS_X = 0,
    AD5290_AXIS_Y = 1,
} ad5290_axis_e;

/* 每轴通道索引（与原理图 1/2/3 号一致） */
typedef enum {
    AD5290_CH_1 = 0,   /* 10K  */
    AD5290_CH_2 = 1,   /* 10K  */
    AD5290_CH_3 = 2,   /* 100K */
} ad5290_ch_e;

#define AD5290_AXIS_NUM        2
#define AD5290_CH_PER_AXIS     3
#define AD5290_TOTAL_NUM       (AD5290_AXIS_NUM * AD5290_CH_PER_AXIS)

#define AD5290_CODE_MIN        0x00U     /* W↔B 端最小阻值 */
#define AD5290_CODE_MID        0x80U     /* 中点（约半阻值） */
#define AD5290_CODE_MAX        0xFFU     /* W↔B 端最大阻值 */

#define AD5290_RAB_10K         10000.0f
#define AD5290_RAB_100K        100000.0f

/**
 * @brief  初始化 IO 起始电平，并把 6 路 RDAC 写为中点 0x80。
 * @note   GPIO 模式已由 CubeMX 在 MX_GPIO_Init() 中配置，本函数不重复初始化。
 */
void AD5290_Init(void);

/**
 * @brief  设置单路 RDAC 码值（0~255）。
 */
void AD5290_SetCode(ad5290_axis_e axis, ad5290_ch_e ch, uint8_t code);

/**
 * @brief  6 路并行写入 RDAC 码值（一次 CS 拉低，6 路 SDA 同步移位）。
 * @param  codes  长度 AD5290_TOTAL_NUM 的码值数组，
 *                索引 = axis * AD5290_CH_PER_AXIS + ch。
 */
void AD5290_SetAllCode(const uint8_t codes[AD5290_TOTAL_NUM]);

/**
 * @brief  按目标阻值（欧姆）设置单路。
 * @note   按 RWB(D) ≈ (D/256) * R_AB 线性换算，忽略 wiper 电阻 Rw。
 *         越界自动钳位到 [0, 255]。
 */
void AD5290_SetOhm(ad5290_axis_e axis, ad5290_ch_e ch, float ohm);

/**
 * @brief  6 路并行按欧姆设置。
 */
void AD5290_SetAllOhm(const float ohms[AD5290_TOTAL_NUM]);

/**
 * @brief  读取驱动内部影子寄存器中的最近一次写入码值。
 *         AD5290 自身只支持只写，故无法回读硬件。
 */
uint8_t AD5290_GetCode(ad5290_axis_e axis, ad5290_ch_e ch);

#ifdef __cplusplus
}
#endif

#endif /* __AD5290_H */
