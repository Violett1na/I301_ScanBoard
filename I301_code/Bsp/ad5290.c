/**
 * @file    ad5290.c
 * @brief   AD5290 数字电位器驱动实现
 *
 * 协议要点（参考 AD5290 数据手册）：
 *   - 8 bit 数据帧，MSB 先发；
 *   - 时钟空闲低电平，SDI 在 SCLK 上升沿被采样（CPOL=0, CPHA=0 / SPI mode 0）；
 *   - CS 拉低期间移位，CS 上升沿锁存到 RDAC；
 *   - SCLK 最高 4 MHz，且 tCSS/tCSH/tCSW/tCH/tCL 都需要满足最小值。
 *   - 显式延时经 port_delay_cycles（平台实现内用周期计数器生成），
 *     避免引脚翻转过快导致器件偶发不锁存。
 *
 * 分层说明（2026-09-02 重构）：引脚操作全部经 port_gpio 契约（引脚表
 *   用板型头 ID），时基经 port_tick 契约；原 GPIO 寄存器直写与厂商
 *   引脚宏已移除。位带时序裕量不变（等效 SCLK <= 2 MHz）。
 */

#include "ad5290.h"
#include "port_gpio.h"
#include "port_tick.h"
#include "i301_ad_da.h"
#include <stddef.h>   /* NULL */

/* -------------------- 引脚映射 --------------------
 * 通道顺序与板型头保持一致。
 */
typedef struct {
    port_pin_t scl;
    port_pin_t sda;
} ad5290_pin_t;

static const ad5290_pin_t s_pins[AD5290_TOTAL_NUM] = {
    /* X 轴 */
    { BOARD_PIN_X_SCL1, BOARD_PIN_X_SDA1 }, /* X-1, 10K  */
    { BOARD_PIN_X_SCL2, BOARD_PIN_X_SDA2 }, /* X-2, 10K  */
    { BOARD_PIN_X_SCL3, BOARD_PIN_X_SDA3 }, /* X-3, 100K */
    /* Y 轴 */
    { BOARD_PIN_Y_SCL1, BOARD_PIN_Y_SDA1 }, /* Y-1, 10K  */
    { BOARD_PIN_Y_SCL2, BOARD_PIN_Y_SDA2 }, /* Y-2, 10K  */
    { BOARD_PIN_Y_SCL3, BOARD_PIN_Y_SDA3 }, /* Y-3, 100K */
};

/* 6 路 SCL 引脚集合: 并行写时一次同步翻转, 避免相位错位 */
static const port_pin_t s_scl_all[AD5290_TOTAL_NUM] = {
    BOARD_PIN_X_SCL1, BOARD_PIN_X_SCL2, BOARD_PIN_X_SCL3,
    BOARD_PIN_Y_SCL1, BOARD_PIN_Y_SCL2, BOARD_PIN_Y_SCL3,
};

/* 各通道满量程电阻，用于欧姆 → 码值换算 */
static const float s_rab[AD5290_TOTAL_NUM] = {
    AD5290_RAB_10K,  AD5290_RAB_10K,  AD5290_RAB_100K,
    AD5290_RAB_10K,  AD5290_RAB_10K,  AD5290_RAB_100K,
};

/* 影子寄存器：AD5290 不支持回读，缓存最近一次写入值供查询使用 */
static uint8_t s_shadow[AD5290_TOTAL_NUM];
static uint32_t s_timing_cycles = 1U;   /* 单次位带延时周期数(≈250ns) */

/* -------------------- 工具函数 -------------------- */

static void ad5290_timing_init(void)
{
    uint32_t hclk_hz = port_sysclk_hz();

    /* 给 AD5290 留出裕量：高/低电平、CS 建立保持统一按 250 ns 控制，等效 SCLK <= 2 MHz */
    s_timing_cycles = (hclk_hz + 3999999U) / 4000000U;
    if (s_timing_cycles == 0U) {
        s_timing_cycles = 1U;
    }
}

static inline void ad5290_bus_delay(void)
{
    port_delay_cycles(s_timing_cycles);
}

static inline uint8_t saturate_u8(int32_t v)
{
    if (v < 0)        return 0U;
    if (v > 0xFF)     return 0xFFU;
    return (uint8_t)v;
}

static inline uint32_t pin_index(ad5290_axis_e axis, ad5290_ch_e ch)
{
    return (uint32_t)axis * AD5290_CH_PER_AXIS + (uint32_t)ch;
}

/**
 * @brief  把 ohm 换算成 0~255 的 RDAC 码值。
 *         RWB(D) ≈ D/256 * R_AB，因此 D = round(ohm/R_AB * 256)。
 */
static uint8_t ohm_to_code(float ohm, float rab)
{
    if (ohm <= 0.0f) return 0U;
    float code_f = (ohm / rab) * 256.0f + 0.5f;
    return saturate_u8((int32_t)code_f);
}

/* -------------------- 单路写入 --------------------
 * SPI mode 0：CLK 空闲低，数据在上升沿被锁存。
 * 写顺序：CS↓ → 移位 8 bit（先 MSB）→ CS↑ 锁存。
 */
static void ad5290_write_single(uint32_t idx, uint8_t code)
{
    const ad5290_pin_t *p = &s_pins[idx];
    int8_t i;

    port_pin_write(BOARD_PIN_AD5290_CS, 0U);
    ad5290_bus_delay();

    for (i = 7; i >= 0; --i) {
        /* 数据先放到 SDA */
        port_pin_write(p->sda, ((code >> i) & 0x01U) ? 1U : 0U);
        ad5290_bus_delay();
        /* 上升沿锁存数据位 */
        port_pin_write(p->scl, 1U);
        ad5290_bus_delay();
        port_pin_write(p->scl, 0U);
        ad5290_bus_delay();
    }

    port_pin_write(BOARD_PIN_AD5290_CS, 1U);
    ad5290_bus_delay();
    s_shadow[idx] = code;
}

/* -------------------- 6 路并行写入 --------------------
 * 同一根 CS、同步时钟，6 根 SDA 一起移位 8 bit。
 * 6 路 SCL 经 port_pin_write_multi 一次性同步翻转（同端口实现合并为
 * 单次寄存器写），避免相位错位。
 */
static void ad5290_write_all(const uint8_t codes[AD5290_TOTAL_NUM])
{
    int8_t i;
    uint32_t ch;

    port_pin_write(BOARD_PIN_AD5290_CS, 0U);
    ad5290_bus_delay();

    for (i = 7; i >= 0; --i) {
        /* 先各自摆好 SDA 位 */
        for (ch = 0; ch < AD5290_TOTAL_NUM; ++ch) {
            const ad5290_pin_t *p = &s_pins[ch];
            port_pin_write(p->sda, ((codes[ch] >> i) & 0x01U) ? 1U : 0U);
        }
        ad5290_bus_delay();
        /* 6 路 SCL 同步上升沿 → 同步下降沿 */
        port_pin_write_multi(s_scl_all, AD5290_TOTAL_NUM, 1U);
        ad5290_bus_delay();
        port_pin_write_multi(s_scl_all, AD5290_TOTAL_NUM, 0U);
        ad5290_bus_delay();
    }

    port_pin_write(BOARD_PIN_AD5290_CS, 1U);
    ad5290_bus_delay();

    for (ch = 0; ch < AD5290_TOTAL_NUM; ++ch) {
        s_shadow[ch] = codes[ch];
    }
}

/* -------------------- 对外接口 -------------------- */

void ad5290_init(void)
{
    uint32_t ch;

    ad5290_timing_init();

    /* 起始空闲电平：CS 高，所有 SCL 低 */
    port_pin_write(BOARD_PIN_AD5290_CS, 1U);
    port_pin_write_multi(s_scl_all, AD5290_TOTAL_NUM, 0U);

    for (ch = 0; ch < AD5290_TOTAL_NUM; ++ch) {
        s_shadow[ch] = 0U;
    }

    /* 码值写入由上层完成（首次为 param_init 经 SetAllCode），见头文件用法 5) */
}

void ad5290_set_code(ad5290_axis_e axis, ad5290_ch_e ch, uint8_t code)
{
    if ((uint32_t)axis >= AD5290_AXIS_NUM)
    {
        return;
    }
    if ((uint32_t)ch >= AD5290_CH_PER_AXIS)
    {
        return;
    }

    ad5290_write_single(pin_index(axis, ch), code);
}

void ad5290_set_all_code(const uint8_t codes[AD5290_TOTAL_NUM])
{
    if (codes == NULL)
    {
        return;
    }
    ad5290_write_all(codes);
}

void ad5290_set_ohm(ad5290_axis_e axis, ad5290_ch_e ch, float ohm)
{
    uint32_t idx;

    if ((uint32_t)axis >= AD5290_AXIS_NUM)
    {
        return;
    }
    if ((uint32_t)ch >= AD5290_CH_PER_AXIS)
    {
        return;
    }

    idx = pin_index(axis, ch);
    ad5290_write_single(idx, ohm_to_code(ohm, s_rab[idx]));
}

void ad5290_set_all_ohm(const float ohms[AD5290_TOTAL_NUM])
{
    uint8_t codes[AD5290_TOTAL_NUM];
    uint32_t i;

    if (ohms == NULL)
    {
        return;
    }

    for (i = 0; i < AD5290_TOTAL_NUM; ++i) {
        codes[i] = ohm_to_code(ohms[i], s_rab[i]);
    }
    ad5290_write_all(codes);
}

uint8_t ad5290_get_code(ad5290_axis_e axis, ad5290_ch_e ch)
{
    if ((uint32_t)axis >= AD5290_AXIS_NUM)
    {
        return 0U;
    }
    if ((uint32_t)ch >= AD5290_CH_PER_AXIS)
    {
        return 0U;
    }

    return s_shadow[pin_index(axis, ch)];
}
