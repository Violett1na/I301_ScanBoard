/* i301_ad_da.h —— 板型配置头: I301 振镜 XY 板(板A, AD-DA 数字通路版)
 * 架构定位: Board 配置轴(四层架构, 见 docs/superpowers/specs/
 *   2026-09-02-portable-layered-architecture-design.md)。
 *   硬件描述区 = 板上布线事实(引脚语义、通道数), 平台实现据此映射物理引脚;
 *   软件配置区 = 本板启用的功能组合。
 * 换板型 = 换本文件(板B 另建); 换芯片不动本文件(见 Platform/<芯片>/)。
 * 分区纪律: 硬件描述区不含策略开关; 模块级诊断/回退开关(如 OCD_SIG_ENABLE)
 *   仍按规范 7-3 留在模块头文件, 本区只放板级功能组合。 */
#ifndef __I301_AD_DA_H__
#define __I301_AD_DA_H__

#include <stdint.h>

/* =================== 硬件描述区 =================== */

/* AD-DA 通道数: 板上布线事实(0=vx 1=ix 2=vy 3=iy) */
#define BOARD_AD_DA_CH_NUM   4U

/* ---- 平台无关引脚 ID(port_pin_t 取值; 物理映射见 Platform/<芯片>/) ---- */
typedef enum
{
    /* AD5290 数字电位器: X/Y 各 3 路, 每路独立 SCL/SDA, 共 1 根 CS */
    BOARD_PIN_AD5290_CS = 0,
    BOARD_PIN_X_SCL1, BOARD_PIN_X_SDA1,   /* X-1, 10K */
    BOARD_PIN_X_SCL2, BOARD_PIN_X_SDA2,   /* X-2, 10K */
    BOARD_PIN_X_SCL3, BOARD_PIN_X_SDA3,   /* X-3, 100K */
    BOARD_PIN_Y_SCL1, BOARD_PIN_Y_SDA1,   /* Y-1, 10K */
    BOARD_PIN_Y_SCL2, BOARD_PIN_Y_SDA2,   /* Y-2, 10K */
    BOARD_PIN_Y_SCL3, BOARD_PIN_Y_SDA3,   /* Y-3, 100K */

    /* 74HC4053 反馈出口选择脚: 高 = 该轴 FB 切 PWM 信令, 低 = 模拟直通 */
    BOARD_PIN_CH_FBX,
    BOARD_PIN_CH_FBY,

    BOARD_PIN_COUNT
} board_pin_e;

/* =================== 软件配置区 =================== */

/* 本板启用 AD-DA 1MHz 数字处理通路(纯模拟板型置 0, 管线实现不参与编译) */
#define BOARD_AD_DA_PIPELINE_ENABLE  1U

#endif /* __I301_AD_DA_H__ */
