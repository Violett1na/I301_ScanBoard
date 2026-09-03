/* port_gpio_g4.c —— STM32G4 实现: 引脚 ID → GPIO 端口/掩码映射
 * 映射依据 Board/i301_ad_da.h 引脚语义与 .ioc 物理分配:
 *   AD5290 全部信号与 4053 选择脚均在 GPIOC。
 * 引脚配置(推挽/上下拉)由生成代码完成, 本文件只做电平写。 */
#include "port_gpio.h"
#include "i301_ad_da.h"
#include "main.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t      mask;
} gpio_map_t;

/* 引脚映射表: 下标 = board_pin_e, 与板型头枚举逐项对应 */
static const gpio_map_t s_map[BOARD_PIN_COUNT] =
{
    [BOARD_PIN_AD5290_CS] = { CS_GPIO_Port,     CS_Pin     },
    [BOARD_PIN_X_SCL1]    = { X_SCL1_GPIO_Port, X_SCL1_Pin },
    [BOARD_PIN_X_SDA1]    = { X_SDA1_GPIO_Port, X_SDA1_Pin },
    [BOARD_PIN_X_SCL2]    = { X_SCL2_GPIO_Port, X_SCL2_Pin },
    [BOARD_PIN_X_SDA2]    = { X_SDA2_GPIO_Port, X_SDA2_Pin },
    [BOARD_PIN_X_SCL3]    = { X_SCL3_GPIO_Port, X_SCL3_Pin },
    [BOARD_PIN_X_SDA3]    = { X_SDA3_GPIO_Port, X_SDA3_Pin },
    [BOARD_PIN_Y_SCL1]    = { Y_SCL1_GPIO_Port, Y_SCL1_Pin },
    [BOARD_PIN_Y_SDA1]    = { Y_SDA1_GPIO_Port, Y_SDA1_Pin },
    [BOARD_PIN_Y_SCL2]    = { Y_SCL2_GPIO_Port, Y_SCL2_Pin },
    [BOARD_PIN_Y_SDA2]    = { Y_SDA2_GPIO_Port, Y_SDA2_Pin },
    [BOARD_PIN_Y_SCL3]    = { Y_SCL3_GPIO_Port, Y_SCL3_Pin },
    [BOARD_PIN_Y_SDA3]    = { Y_SDA3_GPIO_Port, Y_SDA3_Pin },
    [BOARD_PIN_CH_FBX]    = { CH_FBX_GPIO_Port, CH_FBX_Pin },
    [BOARD_PIN_CH_FBY]    = { CH_FBY_GPIO_Port, CH_FBY_Pin },
};

void port_pin_write(port_pin_t pin, uint8_t level)
{
    const gpio_map_t *m;

    if (pin >= BOARD_PIN_COUNT)
    {
        return;   /* 非法引脚: 拒写 */
    }

    m = &s_map[pin];
    if (level != 0U)
    {
        m->port->BSRR = (uint32_t)m->mask;
    }
    else
    {
        m->port->BSRR = ((uint32_t)m->mask) << 16U;
    }
}

void port_pin_write_multi(const port_pin_t pins[], uint8_t n, uint8_t level)
{
    GPIO_TypeDef *port;
    uint32_t      mask = 0U;
    uint8_t       same_port = 1U;
    uint8_t       i;

    if ((pins == NULL) || (n == 0U))
    {
        return;
    }
    if (pins[0] >= BOARD_PIN_COUNT)
    {
        return;   /* 非法引脚: 整批拒写 */
    }

    port = s_map[pins[0]].port;
    for (i = 0U; i < n; i++)
    {
        if (pins[i] >= BOARD_PIN_COUNT)
        {
            return;   /* 非法引脚: 整批拒写 */
        }
        if (s_map[pins[i]].port != port)
        {
            same_port = 0U;
        }
        mask |= (uint32_t)s_map[pins[i]].mask;
    }

    if (same_port != 0U)
    {
        /* 单次 BSRR 写: 多路时钟沿相位一致(位带总线要求) */
        if (level != 0U)
        {
            port->BSRR = mask;
        }
        else
        {
            port->BSRR = mask << 16U;
        }
    }
    else
    {
        /* 跨端口: 退化为逐脚写(本板无此场景, 保通用性) */
        for (i = 0U; i < n; i++)
        {
            port_pin_write(pins[i], level);
        }
    }
}
