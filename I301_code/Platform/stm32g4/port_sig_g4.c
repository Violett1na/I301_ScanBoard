/* port_sig_g4.c —— STM32G4 实现: 过流信令硬件动作
 * 2026-09-02 分层重构: 原 App/ocd_sig.c 固件壳(通路让路/引脚复用/
 *   PWM 时基建立)下沉至此; 检测与决策仍在 App 层(纯状态机)。
 * 信令规格: 两路常开方波 1kHz/50%/3V3, X 轴 PA5(TIM2_CH1)、
 *   Y 轴 PA8(TIM1_CH1), 上电即跑、永不启停; 4053 选择脚经
 *   port_sig_switch_set 切换(物理脚见板型头, 本实现经 port_gpio 写)。
 *
 * 再配置清单(对原有配置的覆盖, 全部集中在 port_sig_init 内):
 *   ① DAC1_CH2 / DAC4_CH2 / OPAMP5 —— port_pipe_init(port_pipe_g4.c)
 *      中启动, 本模块停止且后续不再使能: DAC 反馈功能已废弃
 *      (2026-08-27 用户确认, PA5/PA8 定死 PWM), 管线对这两通道的
 *      DMA 写入保留但无害;
 *   ② 引脚复用 —— PA5 由 DAC1_OUT2 模拟直出、PA8 由 OPAMP5_VOUT
 *      模拟输出, 覆盖为定时器复用推挽: PA5=TIM2_CH1(AF1)、
 *      PA8=TIM1_CH1(AF6); AF 号对照 STM32G474 数据手册复用功能表;
 *      4053 X1/Y1 就挂在这两个网(经 R6/R8 出板到 JB3/JB4), 零改板;
 *   ③ htim2 时基 —— MX_TIM2_Init 原配 PSC=170-1/ARR=99(TRGO=update,
 *      从未启动)被覆盖为 PSC=170-1/ARR=1000-1 的 1kHz PWM 时基,
 *      CH1 增配 PWM1 输出; 原 TRGO=update 主模式保留(本就无受触发者);
 *   ④ TIM2_IRQn —— MspInit 曾使能(优先级 0)但从未配中断源; 本模块
 *      明确不开更新中断, 并在 NVIC 中禁掉(规避旧仓库 TIM2 中断死
 *      代码旧账, README 警示);
 *   ⑤ TIM1 —— 原全工程未用, 本模块新建使用(手工句柄, .ioc 未登记),
 *      不覆盖任何既有外设。
 * 回退边界: App 层 OCD_SIG_ENABLE=0 时本模块不被调用, 上述覆盖
 *   一律不发生, PA5/PA8 保持 DAC 反馈输出(仅作信令模块自身异常时
 *   的应急手段, 正常运行不再使用 DAC 反馈功能)。
 * Fail-safe: 4053 选择脚在生成代码中下拉默认低 = 模拟直通;
 *   MCU 死机信令丢失但反馈链路保持现状(与 OCD 同一固有边界)。 */

#include "port_sig.h"
#include "port_gpio.h"
#include "i301_ad_da.h"
#include "dac.h"
#include "opamp.h"
#include "tim.h"
#include "gpio.h"
#include "main.h"

/* ---- 信令时基(G4 @170MHz → 1kHz/50%; 换芯片须按主频重配) ---- */
#define PORT_SIG_PSC  (170U - 1U)   /* 170MHz/170 = 1MHz 计数时钟(两路同配) */
#define PORT_SIG_ARR  (1000U - 1U)  /* 1MHz/1000 = 1kHz; TIM1 为 16 位,
                                       此为其可容纳方案 */
#define PORT_SIG_CCR  500U          /* 500/1000 = 50% 占空 */

/* ---- 通路让路(再配置①): 覆盖式停用 ----
 * DAC1_CH2/DAC4_CH2/OPAMP5 三通道由 port_pipe_init 启动, 此处停止,
 * 且本特性后续不再使能——DAC 反馈功能废弃, PA5/PA8 定死 PWM 输出。
 * 停止后管线仍按 1MHz 节拍向这两路 DHR 写值(循环 DMA 未动), 写了无害。
 * IN 通道(DAC1_CH1/DAC4_CH1)与 OPAMP4 不在覆盖范围, 保持原样。 */
static void port_sig_route_takeover(void)
{
    if (HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    if (HAL_DAC_Stop(&hdac4, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Stop(&hopamp5)            != HAL_OK) Error_Handler();
}

/* ---- 引脚复用(再配置②): 覆盖原模拟输出为定时器复用推挽 ----
 * PA5: 原模拟功能 = DAC1_OUT2 直出(缓冲 OFF); PA8: 原 = OPAMP5_VOUT。
 *   上一步已停 DAC/OPAMP 让路, 此处把复用选择改成定时器输出。
 * 注: .ioc 中 PA5/PA8 仍登记为模拟, 本处为运行时覆盖, 不改生成代码。 */
static void port_sig_gpio_af_init(void)
{
    GPIO_InitTypeDef gi = {0};

    gi.Pin       = GPIO_PIN_5;          /* X 轴 PWM: PA5 → TIM2_CH1 */
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_NOPULL;
    gi.Speed     = GPIO_SPEED_FREQ_LOW; /* 1kHz 方波, 低摆率利于 EMI */
    gi.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &gi);

    gi.Pin       = GPIO_PIN_8;          /* Y 轴 PWM: PA8 → TIM1_CH1 */
    gi.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(GPIOA, &gi);
}

/* ---- X 轴 PWM(再配置③): 覆盖 htim2 时基, 复用该句柄 ----
 * MX_TIM2_Init 原配 PSC=170-1/ARR=99/TRGO=update(10kHz, 从未启动,
 * 无受触发者), 此处覆盖为 1kHz PWM 时基。
 * 与 TIM1 同配: TIM1 为 16 位定时器(ARR≤65535), 此为其可容纳的 1kHz 方案。
 * 再配置④: 更新中断明确不开; NVIC 禁 TIM2_IRQn —— MspInit 曾使能该
 * 中断号(优先级 0)但从未配源, 此处显式禁掉, 规避旧仓库 TIM2 中断
 * 死代码旧账(回调直写 DHR 与 TX DMA 竞争, README 警示)。
 * 注: HAL_TIM_PWM_Init 见 State=READY(MX_TIM2_Init 已置)不再回调
 * MspInit, 不会重新使能 TIM2_IRQn。 */
static void port_sig_pwm_tim2_init(void)
{
    TIM_OC_InitTypeDef oc = {0};

    htim2.Init.Prescaler         = PORT_SIG_PSC;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = PORT_SIG_ARR;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = PORT_SIG_CCR;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

    if (HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE) != HAL_OK) Error_Handler(); /* PSC 影子装载 */
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

    __HAL_TIM_DISABLE_IT(&htim2, TIM_IT_UPDATE);              /* 纯输出, 不需要中断 */
    HAL_NVIC_DisableIRQ(TIM2_IRQn);                           /* 见函数头注释 */

    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
}

/* ---- Y 轴 PWM(再配置⑤): TIM1 原全工程未用, 本模块新建使用 ----
 * 手工建句柄(.ioc 未登记, 无生成初始化, 不覆盖任何既有外设配置);
 * 时钟在函数内手动使能。
 * TIM1 为高级定时器(带刹车单元), HAL_TIM_PWM_Start 内部处理 MOE。
 * State=RESET 时 HAL_TIM_PWM_Init 回调的是弱定义 HAL_TIM_PWM_MspInit
 * (工程无强定义, 空操作)。
 * ⚠️ TIM1 是 16 位定时器(ARR≤65535): 时基与 TIM2 完全同配,
 * 不可用 ARR=170000-1 大周期方案。 */
static TIM_HandleTypeDef s_htim1;       /* .ioc 未登记, 模块私有 */

static void port_sig_pwm_tim1_init(void)
{
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_TIM1_CLK_ENABLE();
    s_htim1.Instance               = TIM1;
    s_htim1.Init.Prescaler         = PORT_SIG_PSC;
    s_htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim1.Init.Period            = PORT_SIG_ARR;
    s_htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim1.Init.RepetitionCounter = 0U;
    s_htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&s_htim1) != HAL_OK) Error_Handler();

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = PORT_SIG_CCR;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_htim1, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

    if (HAL_TIM_GenerateEvent(&s_htim1, TIM_EVENTSOURCE_UPDATE) != HAL_OK) Error_Handler(); /* PSC 影子装载 */
    __HAL_TIM_SET_COUNTER(&s_htim1, 0U);
    __HAL_TIM_CLEAR_FLAG(&s_htim1, TIM_FLAG_UPDATE);

    if (HAL_TIM_PWM_Start(&s_htim1, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
}

/* ---- 契约接口 ---- */

/* 建立两路常开方波: 轴状态无关, 顺序 = 让路 → 复用 → 起 PWM */
void port_sig_init(void)
{
    port_sig_route_takeover();
    port_sig_gpio_af_init();
    port_sig_pwm_tim2_init();
    port_sig_pwm_tim1_init();
}

/* 4053 反馈出口选择: 物理脚由板型头定义, 经 port_gpio 写 */
void port_sig_switch_set(uint8_t axis, uint8_t on)
{
    port_pin_t pin;

    if (axis == PORT_SIG_AXIS_X)
    {
        pin = BOARD_PIN_CH_FBX;
    }
    else if (axis == PORT_SIG_AXIS_Y)
    {
        pin = BOARD_PIN_CH_FBY;
    }
    else
    {
        return;   /* 非法轴: 拒写 */
    }

    port_pin_write(pin, on);
}

/* file end */
