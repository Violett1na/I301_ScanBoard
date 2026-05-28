/**
 * @file    ad5290.c
 * @brief   AD5290 数字电位器驱动实现
 *
 * 协议要点（参考 AD5290 数据手册）：
 *   - 8 bit 数据帧，MSB 先发；
 *   - 时钟空闲低电平，SDI 在 SCLK 上升沿被采样（CPOL=0, CPHA=0 / SPI mode 0）；
 *   - CS 拉低期间移位，CS 上升沿锁存到 RDAC；
 *   - SCLK 最高 4 MHz，且 tCSS/tCSH/tCSW/tCH/tCL 都需要满足最小值。
 *   - 这里使用 DWT 周期计数器生成显式延时，避免 GPIO 翻转过快导致器件偶发不锁存。
 */

#include "ad5290.h"

/* -------------------- 引脚映射 --------------------
 * 通道顺序与 main.h 原理图保持一致。
 */
typedef struct {
    GPIO_TypeDef *scl_port;
    uint16_t      scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t      sda_pin;
} ad5290_pin_t;

static const ad5290_pin_t s_pins[AD5290_TOTAL_NUM] = {
    /* X 轴 */
    { X_SCL1_GPIO_Port, X_SCL1_Pin, X_SDA1_GPIO_Port, X_SDA1_Pin }, /* X-1, 10K  */
    { X_SCL2_GPIO_Port, X_SCL2_Pin, X_SDA2_GPIO_Port, X_SDA2_Pin }, /* X-2, 10K  */
    { X_SCL3_GPIO_Port, X_SCL3_Pin, X_SDA3_GPIO_Port, X_SDA3_Pin }, /* X-3, 100K */
    /* Y 轴 */
    { Y_SCL1_GPIO_Port, Y_SCL1_Pin, Y_SDA1_GPIO_Port, Y_SDA1_Pin }, /* Y-1, 10K  */
    { Y_SCL2_GPIO_Port, Y_SCL2_Pin, Y_SDA2_GPIO_Port, Y_SDA2_Pin }, /* Y-2, 10K  */
    { Y_SCL3_GPIO_Port, Y_SCL3_Pin, Y_SDA3_GPIO_Port, Y_SDA3_Pin }, /* Y-3, 100K */
};

/* 各通道满量程电阻，用于欧姆 → 码值换算 */
static const float s_rab[AD5290_TOTAL_NUM] = {
    AD5290_RAB_10K,  AD5290_RAB_10K,  AD5290_RAB_100K,
    AD5290_RAB_10K,  AD5290_RAB_10K,  AD5290_RAB_100K,
};

/* 影子寄存器：AD5290 不支持回读，缓存最近一次写入值供查询使用 */
static uint8_t s_shadow[AD5290_TOTAL_NUM];
static uint32_t s_timing_cycles = 1U;
static uint8_t  s_dwt_ready     = 0U;

/* -------------------- 引脚操作内联宏 --------------------
 * 直接走 BSRR，比 HAL_GPIO_WritePin 少一层判断，且为原子操作。
 */
#define PIN_HIGH(port, pin)   ((port)->BSRR = (uint32_t)(pin))
#define PIN_LOW(port, pin)    ((port)->BSRR = ((uint32_t)(pin) << 16U))

#define CS_LOW()              PIN_LOW (CS_GPIO_Port, CS_Pin)
#define CS_HIGH()             PIN_HIGH(CS_GPIO_Port, CS_Pin)

/* -------------------- 工具函数 -------------------- */

static void ad5290_timing_init(void)
{
    uint32_t hclk_hz = HAL_RCC_GetHCLKFreq();

    /* 给 AD5290 留出裕量：高/低电平、CS 建立保持统一按 250 ns 控制，等效 SCLK <= 2 MHz */
    s_timing_cycles = (hclk_hz + 3999999U) / 4000000U;
    if (s_timing_cycles == 0U) {
        s_timing_cycles = 1U;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT       = 0U;
    s_dwt_ready       = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) ? 1U : 0U;
}

static inline void ad5290_delay_cycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    }
}

static inline void ad5290_bus_delay(void)
{
    if (s_dwt_ready != 0U) {
        ad5290_delay_cycles(s_timing_cycles);
    } else {
        for (volatile uint32_t i = 0; i < 16U; ++i) {
            __NOP();
        }
    }
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

    CS_LOW();
    ad5290_bus_delay();

    for (int8_t i = 7; i >= 0; --i) {
        /* 数据先放到 SDA */
        if ((code >> i) & 0x01U) {
            PIN_HIGH(p->sda_port, p->sda_pin);
        } else {
            PIN_LOW (p->sda_port, p->sda_pin);
        }
        ad5290_bus_delay();
        /* 上升沿锁存数据位 */
        PIN_HIGH(p->scl_port, p->scl_pin);
        ad5290_bus_delay();
        PIN_LOW (p->scl_port, p->scl_pin);
        ad5290_bus_delay();
    }

    CS_HIGH();
    ad5290_bus_delay();
    s_shadow[idx] = code;
}

/* -------------------- 6 路并行写入 --------------------
 * 同一根 CS、同步时钟，6 根 SDA 一起移位 8 bit。
 * 因为 6 路 SCL 也都需要翻转，这里把它们整体一起拉高/拉低。
 * GPIOC 上的 6 根 SCL 用 BSRR 一次性切换，避免相位错位。
 */

/* 把 X/Y 6 路 SCL 的位掩码整理出来，便于一次写 BSRR */
#define ALL_SCL_PINS  ( X_SCL1_Pin | X_SCL2_Pin | X_SCL3_Pin \
                      | Y_SCL1_Pin | Y_SCL2_Pin | Y_SCL3_Pin )

static void ad5290_write_all(const uint8_t codes[AD5290_TOTAL_NUM])
{
    /* 6 根 SCL 都在 GPIOC 上，可整体翻转 */
    GPIO_TypeDef *scl_bus = GPIOC;

    CS_LOW();
    ad5290_bus_delay();

    for (int8_t i = 7; i >= 0; --i) {
        /* 先各自摆好 SDA 位 */
        for (uint32_t ch = 0; ch < AD5290_TOTAL_NUM; ++ch) {
            const ad5290_pin_t *p = &s_pins[ch];
            if ((codes[ch] >> i) & 0x01U) {
                PIN_HIGH(p->sda_port, p->sda_pin);
            } else {
                PIN_LOW (p->sda_port, p->sda_pin);
            }
        }
        ad5290_bus_delay();
        /* 6 路 SCL 同步上升沿 → 同步下降沿 */
        scl_bus->BSRR = (uint32_t)ALL_SCL_PINS;             /* set */
        ad5290_bus_delay();
        scl_bus->BSRR = ((uint32_t)ALL_SCL_PINS) << 16U;    /* reset */
        ad5290_bus_delay();
    }

    CS_HIGH();
    ad5290_bus_delay();

    for (uint32_t ch = 0; ch < AD5290_TOTAL_NUM; ++ch) {
        s_shadow[ch] = codes[ch];
    }
}

/* -------------------- 对外接口 -------------------- */

void AD5290_Init(void)
{
    ad5290_timing_init();

    /* 起始空闲电平：CS 高，所有 SCL 低 */
    CS_HIGH();
    GPIOC->BSRR = ((uint32_t)ALL_SCL_PINS) << 16U;

    /* 6 路写入中点 */
    uint8_t codes[AD5290_TOTAL_NUM];
    for (uint32_t i = 0; i < AD5290_TOTAL_NUM; ++i) {
        codes[i] = AD5290_CODE_MID;
    }
    ad5290_write_all(codes);
}

void AD5290_SetCode(ad5290_axis_e axis, ad5290_ch_e ch, uint8_t code)
{
    if ((uint32_t)axis >= AD5290_AXIS_NUM)    return;
    if ((uint32_t)ch   >= AD5290_CH_PER_AXIS) return;

    ad5290_write_single(pin_index(axis, ch), code);
}

void AD5290_SetAllCode(const uint8_t codes[AD5290_TOTAL_NUM])
{
    if (codes == NULL) return;
    ad5290_write_all(codes);
}

void AD5290_SetOhm(ad5290_axis_e axis, ad5290_ch_e ch, float ohm)
{
    if ((uint32_t)axis >= AD5290_AXIS_NUM)    return;
    if ((uint32_t)ch   >= AD5290_CH_PER_AXIS) return;

    uint32_t idx  = pin_index(axis, ch);
    uint8_t  code = ohm_to_code(ohm, s_rab[idx]);
    ad5290_write_single(idx, code);
}

void AD5290_SetAllOhm(const float ohms[AD5290_TOTAL_NUM])
{
    if (ohms == NULL) return;

    uint8_t codes[AD5290_TOTAL_NUM];
    for (uint32_t i = 0; i < AD5290_TOTAL_NUM; ++i) {
        codes[i] = ohm_to_code(ohms[i], s_rab[i]);
    }
    ad5290_write_all(codes);
}

uint8_t AD5290_GetCode(ad5290_axis_e axis, ad5290_ch_e ch)
{
    if ((uint32_t)axis >= AD5290_AXIS_NUM)    return 0U;
    if ((uint32_t)ch   >= AD5290_CH_PER_AXIS) return 0U;

    return s_shadow[pin_index(axis, ch)];
}
