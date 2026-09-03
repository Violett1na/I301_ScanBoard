/* port_tick_g4.c —— STM32G4 实现: 时基与延时原语
 * HAL_GetTick/HAL_Delay 由 SysTick(优先级 15)维护; 周期级延时走 DWT
 * 周期计数器(CMSIS-core 特性, 本文件属平台实现层, 允许使用)。
 * DWT 不可用时(被调试器锁定等)退化为空操作循环, 行为承自原实现。 */
#include "port_tick.h"
#include "chip_cfg.h"
#include "main.h"

static uint8_t s_dwt_ready = 0U;   /* DWT 可用标志(首次调用时探测一次) */
static uint8_t s_dwt_tried = 0U;   /* 已探测标志, 避免反复写使能位 */

static void tick_cycles_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT       = 0U;
    s_dwt_ready       = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) ? 1U : 0U;
    s_dwt_tried       = 1U;
}

uint32_t port_tick_ms(void)
{
    return HAL_GetTick();
}

void port_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void port_delay_cycles(uint32_t n)
{
    uint32_t start;
    uint32_t i;

    if (s_dwt_tried == 0U)
    {
        tick_cycles_init();
    }

    if (s_dwt_ready != 0U)
    {
        start = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start) < n)
        {
        }
    }
    else
    {
        /* 退化路径: 空操作循环(节拍粗, 仅保功能, 承自原实现) */
        for (i = 0U; i < n; i++)
        {
            __NOP();
        }
    }
}

uint32_t port_sysclk_hz(void)
{
    return CHIP_CFG_SYSCLK_HZ;
}
