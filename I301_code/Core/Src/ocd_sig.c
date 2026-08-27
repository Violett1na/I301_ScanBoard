/* ocd_sig.c — 过流 PWM 信令固件模块
 * spec: docs/superpowers/specs/2026-08-27-ocd-pwm-signaling-design.md
 *
 * 职责:
 *   1. 通路让路: 停 DAC1_CH2/DAC4_CH2/OPAMP5, 释放 PA5/PA8;
 *   2. PWM 常开: TIM2_CH1→PA5(X 轴)、TIM1_CH1→PA8(Y 轴),
 *      1kHz/50%/3V3, 上电即跑、永不启停;
 *   3. 算法槽最外层包装(链: linear→ocd→ocd_sig): 按轴独立检测
 *      (纯单元见 ocd_sig.h), 跳闸/释放只写 4053 选择脚 PB3/PB4,
 *      不填样本、不碰 out[]。
 *
 * 再配置清单(对原有配置的覆盖, 全部集中在 ocd_sig_init 内):
 *   ① DAC1_CH2 / DAC4_CH2 / OPAMP5 —— AD_DA_Init(task.c)中启动,
 *      本模块停止且后续不再使能: DAC 反馈功能已废弃(2026-08-27 用户
 *      确认, PA5/PA8 定死 PWM), 管线对这两通道的 DMA 写入保留但无害;
 *   ② 引脚复用 —— PA5 由 DAC1_OUT2 模拟直出、PA8 由 OPAMP5_VOUT
 *      模拟输出, 覆盖为定时器复用推挽: PA5=TIM2_CH1(AF1)、
 *      PA8=TIM1_CH1(AF6);
 *   ③ htim2 时基 —— MX_TIM2_Init 原配 PSC=170-1/ARR=99(TRGO=update,
 *      从未启动)被覆盖为 PSC=170-1/ARR=1000-1 的 1kHz PWM 时基,
 *      CH1 增配 PWM1 输出; 原 TRGO=update 主模式保留(本就无受触发者,
 *      保留无害);
 *   ④ TIM2_IRQn —— MspInit 曾使能(优先级 0)但从未配中断源; 本模块
 *      明确不开更新中断, 并在 NVIC 中禁掉(规避 task.c TIM2 中断死
 *      代码旧账, README 警示);
 *   ⑤ TIM1 —— 原全工程未用, 本模块新建使用(手工句柄, 同 task.c
 *      htim6 先例), 不覆盖任何既有外设。
 *   回退边界: OCD_SIG_ENABLE=0 时上述覆盖一律不发生, PA5/PA8 保持
 *   DAC 反馈输出——该回退仅作信令模块自身异常时的应急手段, 正常
 *   运行不再使用 DAC 反馈功能。
 *
 * Fail-safe: PB3/PB4 在 gpio.c 下拉默认低 = 模拟直通; MCU 死机
 *   信令丢失但反馈链路保持现状(与 ocd 同一固有边界, spec §8)。
 * 时序: 计时用 HAL_GetTick()(ISR 内读, 误差 ≤1ms), 同 ocd 惯例。 */

#include "ocd_sig.h"
#include "task.h"     /* ad_da_process_fn_t / AD_DA_CH_NUM / ad_da_process_fn */

#if OCD_SIG_ENABLE

/* ---- 模块内部状态 ---- */
static ad_da_process_fn_t s_inner;      /* 被包装的内层算法(= ocd 包装层) */
static ocd_sig_axis_t     s_axis_x;     /* X 轴状态机, 检测源 ix = in[1] */
static ocd_sig_axis_t     s_axis_y;     /* Y 轴状态机, 检测源 iy = in[3] */

/* ---- 通路让路(再配置①): 覆盖式停用 ----
 * DAC1_CH2/DAC4_CH2/OPAMP5 三通道由 AD_DA_Init(task.c)启动, 此处停止,
 * 且本特性后续不再使能——DAC 反馈功能废弃, PA5/PA8 定死 PWM 输出。
 * 停止后管线仍按 1MHz 节拍向这两路 DHR 写值(循环 DMA 未动), 写了无害。
 * IN 通道(DAC1_CH1/DAC4_CH1)与 OPAMP4 不在覆盖范围, 保持原样。 */
static void ocd_sig_route_takeover(void)
{
    if (HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    if (HAL_DAC_Stop(&hdac4, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Stop(&hopamp5)            != HAL_OK) Error_Handler();
}

/* ---- 引脚复用(再配置②): 覆盖原模拟输出为定时器复用推挽 ----
 * PA5: 原模拟功能 = DAC1_OUT2 直出(缓冲 OFF); PA8: 原 = OPAMP5_VOUT。
 *   上一步已停 DAC/OPAMP 让路, 此处把复用选择改成定时器输出:
 *   PA5 = TIM2_CH1(AF1), PA8 = TIM1_CH1(AF6)。
 * AF 号对照 STM32G474 数据手册复用功能表确认(见 Task 0 终审记录):
 *   PA5: AF1 = TIM2_CH1; PA8: AF6 = TIM1_CH1。
 * 4053 X1/Y1 就挂在这两个网(经 R6/R8 出板到 JB3/JB4), 零改板。
 * 注: .ioc 中 PA5/PA8 仍登记为模拟, 本处为运行时覆盖, 不改生成代码。 */
static void ocd_sig_gpio_af_init(void)
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
 * 无受触发者), 此处覆盖为 1kHz PWM 时基:
 *   PSC 保持 170-1(170MHz/170=1MHz), ARR 由 99 覆盖为 1000-1
 *   (1MHz/1000=1kHz), CCR=500(50%), CH1 增配 PWM1 输出;
 *   原 TRGO=update 主模式保留不动(本就无人受触发, 保留无害)。
 * 与 TIM1 同配: TIM1 为 16 位定时器(ARR≤65535), 此为其可容纳的 1kHz 方案。
 * 再配置④: 更新中断明确不开; NVIC 禁 TIM2_IRQn —— MspInit 曾使能该
 * 中断号(优先级 0)但从未配源, 此处显式禁掉, 规避 task.c TIM2 中断
 * 死代码旧账(回调直写 DHR 与 TX DMA 竞争, README 警示)。
 * 注: HAL_TIM_PWM_Init 见 State=READY(MX_TIM2_Init 已置)不再回调
 * MspInit, 不会重新使能 TIM2_IRQn。 */
static void ocd_sig_pwm_tim2_init(void)
{
    TIM_OC_InitTypeDef oc = {0};

    htim2.Init.Prescaler         = OCD_SIG_PSC;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = OCD_SIG_ARR;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = OCD_SIG_CCR;
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
 * 按 task.c htim6 先例手工建句柄(.ioc 未登记, 无 CubeMX 生成初始化,
 * 不覆盖任何既有外设配置); 时钟在函数内手动使能。
 * TIM1 为高级定时器(带刹车单元), HAL_TIM_PWM_Start 内部处理 MOE。
 * State=RESET 时 HAL_TIM_PWM_Init 回调的是弱定义 HAL_TIM_PWM_MspInit
 * (工程无强定义, 空操作)。
 * ⚠️ TIM1 是 16 位定时器(ARR≤65535): 时基与 TIM2 完全同配
 * (PSC=170-1/ARR=1000-1/CCR=500), 不可用 ARR=170000-1 大周期方案。 */
static TIM_HandleTypeDef s_htim1;       /* .ioc 未登记, 模块私有 */

static void ocd_sig_pwm_tim1_init(void)
{
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_TIM1_CLK_ENABLE();
    s_htim1.Instance               = TIM1;
    s_htim1.Init.Prescaler         = OCD_SIG_PSC;
    s_htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim1.Init.Period            = OCD_SIG_ARR;
    s_htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim1.Init.RepetitionCounter = 0U;
    s_htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&s_htim1) != HAL_OK) Error_Handler();

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = OCD_SIG_CCR;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_htim1, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

    if (HAL_TIM_GenerateEvent(&s_htim1, TIM_EVENTSOURCE_UPDATE) != HAL_OK) Error_Handler(); /* PSC 影子装载 */
    __HAL_TIM_SET_COUNTER(&s_htim1, 0U);
    __HAL_TIM_CLEAR_FLAG(&s_htim1, TIM_FLAG_UPDATE);

    if (HAL_TIM_PWM_Start(&s_htim1, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
}

/* ---- 算法槽最外层包装: 逐样本检测 + 只切 4053 选择脚,
 *      out[] 无条件委托内层(本模块不改任何通道输出) ---- */
static void ocd_sig_process(const uint16_t *in[AD_DA_CH_NUM],
                            uint16_t       *out[AD_DA_CH_NUM],
                            uint16_t        n)
{
    uint32_t        now = HAL_GetTick();
    const uint16_t *pix = in[1];   /* ix */
    const uint16_t *piy = in[3];   /* iy */

    for (uint16_t i = 0U; i < n; i++)
    {
        uint8_t evx = ocd_sig_feed(&s_axis_x, pix[i], now);
        uint8_t evy = ocd_sig_feed(&s_axis_y, piy[i], now);

        if (evx == OCD_SIG_EV_TRIP)
        {
            HAL_GPIO_WritePin(CH_FBX_GPIO_Port, CH_FBX_Pin, GPIO_PIN_SET);
        }
        else if (evx == OCD_SIG_EV_RELEASE)
        {
            HAL_GPIO_WritePin(CH_FBX_GPIO_Port, CH_FBX_Pin, GPIO_PIN_RESET);
        }

        if (evy == OCD_SIG_EV_TRIP)
        {
            HAL_GPIO_WritePin(CH_FBY_GPIO_Port, CH_FBY_Pin, GPIO_PIN_SET);
        }
        else if (evy == OCD_SIG_EV_RELEASE)
        {
            HAL_GPIO_WritePin(CH_FBY_GPIO_Port, CH_FBY_Pin, GPIO_PIN_RESET);
        }
    }

#if OCD_SIG_FORCE
    /* 诊断: 强置两轴信令开(每拍覆写, 压过状态机的一切切换;
       只影响电气链路验证, 不改动状态机自身计数) */
    HAL_GPIO_WritePin(CH_FBX_GPIO_Port, CH_FBX_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CH_FBY_GPIO_Port, CH_FBY_Pin, GPIO_PIN_SET);
#endif

    s_inner(in, out, n);   /* 透传: 检测与输出完全解耦 */
}

/* ---- 挂载: main.c 中 ocd_init() 之后调用一次 ----
 * 顺序: 轴状态 → 通路让路 → 引脚复用 → 起 PWM → 包装算法槽(最后赋值)。
 * 全部硬件再配置(对原配置的覆盖)集中在本函数内, 逐项见文件头
 * "再配置清单①~⑤": OCD_SIG_ENABLE=0 时这些覆盖一律不发生、无残留。 */
void ocd_sig_init(void)
{
    ocd_sig_axis_init(&s_axis_x);
    ocd_sig_axis_init(&s_axis_y);

    ocd_sig_route_takeover();
    ocd_sig_gpio_af_init();
    ocd_sig_pwm_tim2_init();
    ocd_sig_pwm_tim1_init();

    s_inner          = ad_da_process_fn;
    ad_da_process_fn = ocd_sig_process;   /* 最后赋值: 此后管线经包装层 */
}

/* ---- 观测接口(主循环/日志用; 单字节读在 Cortex-M 上原子) ---- */
uint8_t ocd_sig_state(void)
{
    return (uint8_t)(s_axis_x.on | (uint8_t)(s_axis_y.on << 1));
}

uint32_t ocd_sig_trip_count(void)
{
    return s_axis_x.trips + s_axis_y.trips;
}

#else /* !OCD_SIG_ENABLE —— 编译期整体摘除, 空实现保调用点免改 */

void ocd_sig_init(void) {}
uint8_t ocd_sig_state(void) { return 0U; }
uint32_t ocd_sig_trip_count(void) { return 0U; }

#endif /* OCD_SIG_ENABLE */
