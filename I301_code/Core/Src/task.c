#include "task.h"
#include "bsp_flash.h"
#include <stdio.h>

/* ------------------------------------------------------------------
 * 诊断模式开关(全部宏集中文件头, 统一管理)
 *
 * AD_DA_PATTERN_DIRECT_ENABLE: 1 = 点阵直出诊断模式(屏蔽 ADC/DMA 管线),
 *   0 = 走 1MHz AD-DA 跟随通路。
 * MSB_TEST_ENABLE(task.h): 1 = major-carry 对照测试, 必须走跟随通路,
 *   与 pattern-direct 互斥(下方编译互锁)。
 * AD_DA_CONST_ENABLE: 1 = 算法槽换常量诊断: 冻结轴 IN 2048/FB 0、
 *   另一轴正常线性跟随(保图像), 斩断冻结轴的 ADC→DAC 噪声回灌环,
 *   环路归因诊断用; 0 = 正常线性跟随通路。诊断完务必改回 0。
 * AD_DA_CONST_AXIS: 冻结哪根轴(X 或 Y)。
 * AD_DA_DEADBAND_ENABLE: 1 = IN 通道死区(|Δx|<LSB 保持上次接受值),
 *   斩边界 1LSB 闭环回灌(8-21 断环实证回灌); 实测 LSB=5 粗步进上板更抖
 *   (保持-大跳量化落在可见带), 弃用保留对照; 0 = 关。
 * AD_DA_IN_LPF_ENABLE: 1 = IN 通道一阶 IIR 低滤(α=1/2^K, K=AD_DA_IN_LPF_SHIFT);
 *   8-21 上板否决: 指峰被拉成指数尾, 能量压进伺服带, 图像变形+抖更重。
 * AD_DA_IN_MED3_ENABLE: 1 = IN 通道因果中值-3, 只删孤立指峰( kickback/313kHz
 *   尖峰混叠): 中值性质使孤立冲激永不进输出, 无阶梯、群延时≈0, 不伤图案边沿。
 *   三者(死区/IIR/中值)互斥(编译互锁)。FB 通道不受影响。
 * ------------------------------------------------------------------ */
#define AD_DA_PATTERN_DIRECT_ENABLE 0
#define AD_DA_CONST_ENABLE          0   /* 8-21 验证1b: 双轴全冻再扫发生器, 平台仍在=DAC全无关 */
#define AD_DA_DEADBAND_ENABLE       0U
#define AD_DA_DEADBAND_LSB          5U
#define AD_DA_IN_LPF_ENABLE         0U   /* 8-21 上板: α=1/16 滞后吃掉相位裕度, 图像变形振铃, 回退 */
#define AD_DA_IN_LPF_SHIFT          4U   /* α=1/16, 时滞≈16µs, 白噪 σ↓≈×5.6 */
#define AD_DA_IN_MED3_ENABLE        0U
#define ADC_SOLO                    0U   /* 0=全开; 3/4=仅该ADC转换; 255=全停(纯模拟观测) */
#define ADC_SOLO_CH4_ENABLE         (ADC_SOLO == 4U)
#define AD_DA_CONST_AXIS_X          0U
#define AD_DA_CONST_AXIS_Y          1U
#define AD_DA_CONST_AXIS_BOTH       2U
#define AD_DA_CONST_AXIS            AD_DA_CONST_AXIS_BOTH

/* 冻结轴通道归属: ch0/1=X, ch2/3=Y, BOTH=全冻 */
#define AD_DA_CONST_FROZEN_CH(ch) \
    ((AD_DA_CONST_AXIS == AD_DA_CONST_AXIS_BOTH) ? 1U : \
     ((AD_DA_CONST_AXIS == AD_DA_CONST_AXIS_X) ? ((ch) <= 1U) : ((ch) >= 2U)))

#if MSB_TEST_ENABLE
#if AD_DA_PATTERN_DIRECT_ENABLE
#error "MSB_TEST_ENABLE 要求 AD_DA_PATTERN_DIRECT_ENABLE == 0"
#endif
#define MSB_TESTING_X (MSB_TEST_AXIS == MSB_TEST_AXIS_X)
#define MSB_TESTING_Y (MSB_TEST_AXIS == MSB_TEST_AXIS_Y)
#else
#define MSB_TESTING_X 0
#define MSB_TESTING_Y 0
#endif

#if (AD_DA_IN_LPF_ENABLE + AD_DA_IN_MED3_ENABLE + AD_DA_DEADBAND_ENABLE) > 1
#error "AD_DA_IN_LPF_ENABLE / AD_DA_IN_MED3_ENABLE / AD_DA_DEADBAND_ENABLE 三选一"
#endif

#define AD_DA_PATTERN_LEN  383U               /* 每通道点数 */
#define AD_DA_PATTERN_ARR  2833U              /* TIM3 ARR: 170MHz/(2×2833) ≈ 30.0035kHz 点率, 整帧 ≈78.3Hz */

#define MSB_VOFA_MODE 1U   /* 1 = 串口仅输出 "%d %d\n" DAC 码值流(供 VOFA), 抑制快照/轮转日志 */

#if MSB_TESTING_X
#define MSB_MON_CH 1U   /* X 轴观 ix(功率级侧) */
#else
#define MSB_MON_CH 3U   /* Y 轴观 iy */
#endif

/* ---- MSB major-carry 对照测试状态(须在 ADC 回调之前声明) ---- */
static const uint16_t msb_code[3][2] = {
    {2047U, 2048U},   /* A: 12 位同时翻转(major-carry) */
    {2046U, 2047U},   /* B: 单比特翻转对照 */
    {2048U, 2049U},   /* C: 单比特翻转对照 */
};
static volatile uint8_t  msb_sel;             /* 当前对照组, 主循环轮转 */
static uint8_t           msb_phase;           /* 乒乓相位, TIM6 ISR 独享 */
static uint16_t          msb_snap[MSB_SNAP_LEN]; /* 电流量快照, ISR 写主循环读 */
static volatile uint8_t  msb_snap_ready;
static uint16_t          msb_snap_cnt;
static volatile uint16_t msb_min = 4095U;     /* 窗口统计, ISR 写主循环读 */
static volatile uint16_t msb_max;
static volatile uint32_t msb_outlier;
static volatile uint16_t msb_base;            /* 离群基准=上窗中值, 主循环写 ISR 读 */
TIM_HandleTypeDef        htim6;               /* MSB 步进定时器, 手工创建(.ioc 未登记) */

flash_store_t flash_store;
volatile adc_value_t adc_value;

radc_value_t radc_value;
comp_value_t comp_value;

/* ------------------------------------------------------------------
 * AD-DA 处理通路（1MHz 逐样本线性处理、预留算法槽）
 *
 * 数据流: TIM3 TRGO(1MHz) → 4路ADC → rx_buf → 块处理 → tx_buf → 4路DAC
 *   rx_buf[0]=vx(ADC3/PB13) → tx_buf[0] → DAC1_CH1 → PA4  (DA_INX)
 *   rx_buf[1]=ix(ADC2/PA0)  → tx_buf[1] → DAC1_CH2 → PA5  (DA_FBX)
 *   rx_buf[2]=vy(ADC4/PB15) → tx_buf[2] → DAC4_CH1 → PB12 (DA_INY, 经OPAMP4跟随)
 *   rx_buf[3]=iy(ADC5/PA9)  → tx_buf[3] → DAC4_CH2 → PA8  (DA_FBY, 经OPAMP5跟随)
 *
 * 半块相位合约(无锁核心): RX 半块完成时刻 = TX 进入同一半块时刻(同 TRGO)
 * ⇒ ISR 拥有完整 32µs 窗口写该半块；若超死线, TX 重读旧值 = 输出保持, 不崩。
 * 注意: 块处理必须 << 32µs, 否则同级中断背靠背、CPU 被锁死在 ISR
 * (上板教训: -O0 下朴素双层循环 128 样本 >32µs, TIM3 一启动整机即冻结)。
 * ------------------------------------------------------------------ */
static uint16_t rx_buf[AD_DA_CH_NUM][AD_DA_BLOCK];   /* RX: ADC 循环 DMA 写入 */
static uint16_t tx_buf[AD_DA_CH_NUM][AD_DA_BLOCK];   /* TX: DMA 循环读出至 DAC DHR */
#if AD_DA_DEADBAND_ENABLE
static uint16_t db_last[AD_DA_CH_NUM];               /* 死区: 每通道上次接受输入值(仅 IN 通道用) */
#endif
#if AD_DA_IN_LPF_ENABLE
static int32_t  in_filt[AD_DA_CH_NUM];               /* IIR 低滤状态(仅 IN 通道用) */
static uint8_t  in_filt_init[AD_DA_CH_NUM];          /* 首样本初始化标志 */
#endif
#if AD_DA_IN_MED3_ENABLE
static uint16_t med_h1[AD_DA_CH_NUM];                /* 中值-3 历史: 上上样本 */
static uint16_t med_h2[AD_DA_CH_NUM];                /* 中值-3 历史: 上一样本 */
#endif

/* 默认线性算法(IN/FB 通道分制, 饱和钳位 0..4095):
 *   IN 通道 ch0/ch2(vx→DA_INX, vy→DA_INY): y = (4095 - x) + off
 *     —— 符号分析见 spec §1: 抵消 ADC-V 调理反相, 进 U1C 求和点端到端符号为正;
 *   FB 通道 ch1/ch3(ix→DA_FBX, iy→DA_FBY): y = x + off
 *     —— 反馈导出要求(2026-08-06 上板确认, spec §9.7): 静息 0V、有激励时出波形,
 *     不反相; 静息 i≈0 ⇒ y=off 钳到 0; 原统一反相公式会把 FB 静息点顶到 ≈2.2V。
 * off 取协议补偿值: ch0/ch1(X轴)用 comp_value.x, ch2/ch3(Y轴)用 comp_value.y
 * (comp_value 主循环写、ISR 读, 对齐加载天然原子, 不加锁)
 * 刻度: 1 LSB ≈ 0.61mV(DAC端) ≈ 1.22mV(模拟求和点, 经 U15 ×2)；off 为偏置叠加(调零), 非校准
 *
 * 性能要点(32µs 死线, 每次 4 通道 × 32 样本; 本工程按 -O0 构建, 每周期都很贵):
 * 1) off 预合并为 base, 每样本只剩一次加/减法;
 * 2) 指针步进代替二维下标, 消除 -O0 下双层索引的栈往返;
 * 3) __USAT(val,12) 为 M4 硬件饱和指令, 单指令完成 0..4095 双向钳位;
 * 4) 循环二重展开, 减半循环记账开销;
 * 5) 反相/同相分支在样本循环之外(每通道判定一次, 无逐样本判断);
 * 6) IN 通道降噪三选一: 死区 / 一阶 IIR / 因果中值-3(对应 ENABLE 宏),
 *    每样本多几次整数 ALU, -O2 下远裕于 32µs 死线。
 * 若死线仍吃紧, 可在 EIDE 中仅对 task.c 提高优化等级(其余文件保持 -O0)。 */
/* 单通道线性内核(IN 反相 / FB 同相, 饱和钳位),
 * ad_da_process_linear 与常量诊断 ad_da_process_const 共用。
 * ch: 通道号, 低滤/死区状态按 ch 取 in_filt/db_last(仅 IN 通道用)。 */
static void ad_da_chan_linear(const uint16_t *p_in,
                              uint16_t       *p_out,
                              uint16_t        cnt,
                              int32_t         b,
                              uint8_t         inv,
                              uint8_t         ch)
{
    if (inv != 0U)                    /* IN 通道: 反相 */
    {
#if AD_DA_IN_MED3_ENABLE
        /* 因果中值-3: 孤立冲激永不进输出(中值性质), 群延时≈0, 不伤边沿 */
        if (med_h1[ch] == 0U && med_h2[ch] == 0U)
        {
            med_h1[ch] = p_in[0];   /* 首样本播种, 免上电 0 码毛刺 */
            med_h2[ch] = p_in[0];
        }
        while (cnt >= 2U)             /* 二重展开 */
        {
            uint16_t x  = p_in[0];
            uint16_t h1 = med_h1[ch];
            uint16_t h2 = med_h2[ch];
            uint16_t hi = (x > h1) ? x : h1;
            uint16_t lo = (x < h1) ? x : h1;
            uint16_t m0 = (h2 > hi) ? hi : ((h2 < lo) ? lo : h2);
            med_h2[ch] = h1;
            med_h1[ch] = x;
            x  = p_in[1];
            h1 = med_h1[ch];
            h2 = med_h2[ch];
            hi = (x > h1) ? x : h1;
            lo = (x < h1) ? x : h1;
            uint16_t m1 = (h2 > hi) ? hi : ((h2 < lo) ? lo : h2);
            med_h2[ch] = h1;
            med_h1[ch] = x;
            p_out[0] = (uint16_t)__USAT(b - (int32_t)m0, 12U);
            p_out[1] = (uint16_t)__USAT(b - (int32_t)m1, 12U);
            p_in  += 2U;
            p_out += 2U;
            cnt   -= 2U;
        }
        if (cnt != 0U)                /* 奇数尾巴(n=32 不会走到, 保通用性) */
        {
            uint16_t x  = *p_in;
            uint16_t h1 = med_h1[ch];
            uint16_t h2 = med_h2[ch];
            uint16_t hi = (x > h1) ? x : h1;
            uint16_t lo = (x < h1) ? x : h1;
            uint16_t m0 = (h2 > hi) ? hi : ((h2 < lo) ? lo : h2);
            med_h2[ch] = h1;
            med_h1[ch] = x;
            *p_out = (uint16_t)__USAT(b - (int32_t)m0, 12U);
        }
#elif AD_DA_IN_LPF_ENABLE
        /* 一阶 IIR 低滤: 噪声进 DAC 前平均掉, 输出连续小步 */
        if (in_filt_init[ch] == 0U)
        {
            in_filt_init[ch] = 1U;
            in_filt[ch]      = (int32_t)p_in[0];
        }
        while (cnt >= 2U)             /* 二重展开 */
        {
            int32_t f  = in_filt[ch];
            int32_t x0 = p_in[0];
            int32_t x1 = p_in[1];
            f += (x0 - f + (1 << (AD_DA_IN_LPF_SHIFT - 1))) >> AD_DA_IN_LPF_SHIFT;
            x0  = f;
            f += (x1 - f + (1 << (AD_DA_IN_LPF_SHIFT - 1))) >> AD_DA_IN_LPF_SHIFT;
            x1  = f;
            in_filt[ch] = f;
            p_out[0] = (uint16_t)__USAT(b - x0, 12U);
            p_out[1] = (uint16_t)__USAT(b - x1, 12U);
            p_in  += 2U;
            p_out += 2U;
            cnt   -= 2U;
        }
        if (cnt != 0U)                /* 奇数尾巴(n=32 不会走到, 保通用性) */
        {
            int32_t f  = in_filt[ch];
            f += ((int32_t)*p_in - f + (1 << (AD_DA_IN_LPF_SHIFT - 1))) >> AD_DA_IN_LPF_SHIFT;
            in_filt[ch] = f;
            *p_out = (uint16_t)__USAT(b - f, 12U);
        }
#elif AD_DA_DEADBAND_ENABLE
        /* 死区(对照保留): |x - 上次接受值| < LSB 保持, 斩边界 1LSB 回灌 */
        while (cnt >= 2U)             /* 二重展开 */
        {
            uint16_t x0 = p_in[0];
            uint16_t x1 = p_in[1];
            int32_t  d  = (int32_t)x0 - (int32_t)db_last[ch];
            if (d < 0) { d = -d; }
            if (d >= AD_DA_DEADBAND_LSB) { db_last[ch] = x0; } else { x0 = db_last[ch]; }
            d = (int32_t)x1 - (int32_t)db_last[ch];
            if (d < 0) { d = -d; }
            if (d >= AD_DA_DEADBAND_LSB) { db_last[ch] = x1; } else { x1 = db_last[ch]; }
            p_out[0] = (uint16_t)__USAT(b - (int32_t)x0, 12U);
            p_out[1] = (uint16_t)__USAT(b - (int32_t)x1, 12U);
            p_in  += 2U;
            p_out += 2U;
            cnt   -= 2U;
        }
        if (cnt != 0U)                /* 奇数尾巴(n=32 不会走到, 保通用性) */
        {
            uint16_t x0 = *p_in;
            int32_t  d  = (int32_t)x0 - (int32_t)db_last[ch];
            if (d < 0) { d = -d; }
            if (d >= AD_DA_DEADBAND_LSB) { db_last[ch] = x0; } else { x0 = db_last[ch]; }
            *p_out = (uint16_t)__USAT(b - (int32_t)x0, 12U);
        }
#else
        while (cnt >= 2U)             /* 二重展开 */
        {
            p_out[0] = (uint16_t)__USAT(b - (int32_t)p_in[0], 12U);
            p_out[1] = (uint16_t)__USAT(b - (int32_t)p_in[1], 12U);
            p_in  += 2U;
            p_out += 2U;
            cnt   -= 2U;
        }
        if (cnt != 0U)                /* 奇数尾巴(n=32 不会走到, 保通用性) */
        {
            *p_out = (uint16_t)__USAT(b - (int32_t)*p_in, 12U);
        }
#endif
    }
    else                              /* FB 通道: 同相 */
    {
        while (cnt >= 2U)             /* 二重展开 */
        {
            p_out[0] = (uint16_t)__USAT(b + (int32_t)p_in[0], 12U);
            p_out[1] = (uint16_t)__USAT(b + (int32_t)p_in[1], 12U);
            p_in  += 2U;
            p_out += 2U;
            cnt   -= 2U;
        }
        if (cnt != 0U)                /* 奇数尾巴(n=32 不会走到, 保通用性) */
        {
            *p_out = (uint16_t)__USAT(b + (int32_t)*p_in, 12U);
        }
    }
}

void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM],
                          uint16_t       *out[AD_DA_CH_NUM],
                          uint16_t        n)
{
    int32_t base[AD_DA_CH_NUM];
    uint8_t inv[AD_DA_CH_NUM];

    /* IN 通道: y = (4095 + off) - x */
    base[0] = 4095 + (int32_t)comp_value.x;   inv[0] = 1U;   /* vx → DA_INX */
    base[2] = 4095 + (int32_t)comp_value.y;   inv[2] = 1U;   /* vy → DA_INY */
    /* FB 通道: y = off + x */
    base[1] = (int32_t)comp_value.x;          inv[1] = 0U;   /* ix → DA_FBX */
    base[3] = (int32_t)comp_value.y;          inv[3] = 0U;   /* iy → DA_FBY */

    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        ad_da_chan_linear(in[ch], out[ch], n, base[ch], inv[ch], ch);
    }
}

/* 常量输出算法(断环诊断): 冻结轴(AD_DA_CONST_AXIS)忽略输入,
 * IN 通道 2048 中点、FB 通道 0; 另一轴正常线性跟随(保图像)。
 * 冻结轴 DAC 输出安静直流(与预填 idle 值一致, 振镜安全),
 * 其 ADC 采样不受影响——冻结轴通道抖动消失 = 该轴回灌环实锤。 */
void ad_da_process_const(const uint16_t *in[AD_DA_CH_NUM],
                         uint16_t       *out[AD_DA_CH_NUM],
                         uint16_t        n)
{
    int32_t base[AD_DA_CH_NUM];
    uint8_t inv[AD_DA_CH_NUM];

    base[0] = 4095 + (int32_t)comp_value.x;   inv[0] = 1U;
    base[2] = 4095 + (int32_t)comp_value.y;   inv[2] = 1U;
    base[1] = (int32_t)comp_value.x;          inv[1] = 0U;
    base[3] = (int32_t)comp_value.y;          inv[3] = 0U;

    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        if (AD_DA_CONST_FROZEN_CH(ch))
        {
            const uint16_t v = ((ch == 1U) || (ch == 3U)) ? 0U : 2048U;
            uint16_t *p_out = out[ch];
            uint16_t  cnt   = n;
            while (cnt-- != 0U)
            {
                *p_out++ = v;
            }
        }
        else
        {
            ad_da_chan_linear(in[ch], out[ch], n, base[ch], inv[ch], ch);
        }
    }
}

/* 算法槽: 运行时可换的处理函数(暂不分配协议命令字, YAGNI) */
#if AD_DA_CONST_ENABLE
ad_da_process_fn_t ad_da_process_fn = ad_da_process_const;
#else
ad_da_process_fn_t ad_da_process_fn = ad_da_process_linear;
#endif

/* 块处理: 处理自 offset 起的一个半块(全部通道) */
static void ad_da_process_half(uint16_t offset)
{
    const uint16_t *in[AD_DA_CH_NUM];
    uint16_t       *out[AD_DA_CH_NUM];

    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        in[ch]  = &rx_buf[ch][offset];
        out[ch] = &tx_buf[ch][offset];
    }
    ad_da_process_fn(in, out, AD_DA_HALF);
}

/* 块处理节拍: 仅以 hadc3(DMA1_Ch1)的 HT/TC 为全局调度点。
 * ISR 内容为纯数据搬运 + 整数 ALU(实时数据通路, AGENTS.md 中断规范
 * 的已确认例外条款), 严禁在此调用协议/flash/日志。 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
#if ADC_SOLO_CH4_ENABLE
    if (hadc->Instance == ADC4)
#else
    if (hadc->Instance == ADC3)
#endif
    {
        ad_da_process_half(0U);                 /* 前半块 */
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
#if ADC_SOLO_CH4_ENABLE
    if (hadc->Instance == ADC4)
#else
    if (hadc->Instance == ADC3)
#endif
    {
        ad_da_process_half(AD_DA_HALF);         /* 后半块 */

        /* 顺带更新监测值: 取本块最后一个样本(保留监测/上报口) */
#if !ADC_SOLO_CH4_ENABLE
        adc_value.vx = rx_buf[0][AD_DA_BLOCK - 1U];
        adc_value.ix = rx_buf[1][AD_DA_BLOCK - 1U];
#endif
        adc_value.vy = rx_buf[2][AD_DA_BLOCK - 1U];
#if !ADC_SOLO_CH4_ENABLE
        adc_value.iy = rx_buf[3][AD_DA_BLOCK - 1U];
#endif

#if MSB_TEST_ENABLE
        /* MSB 测试: 被测轴电流量窗口统计 + 快照(纯整数 ALU, 实时通路例外条款) */
        {
            const uint16_t *p = rx_buf[MSB_MON_CH];
            for (uint16_t i = 0; i < AD_DA_BLOCK; i++)
            {
                uint16_t v = p[i];
                if (msb_snap_ready == 0U)
                {
                    msb_snap[msb_snap_cnt++] = v;
                    if (msb_snap_cnt >= MSB_SNAP_LEN)
                    {
                        msb_snap_ready = 1U;
                        msb_snap_cnt   = 0U;
                    }
                }
                if (v < msb_min) msb_min = v;
                if (v > msb_max) msb_max = v;
                int32_t d = (int32_t)v - (int32_t)msb_base;
                if ((d > MSB_OUTLIER_TH) || (d < -MSB_OUTLIER_TH)) msb_outlier++;
            }
        }
#endif
    }
}

/* ------------------------------------------------------------------
 * AD-DA 处理通路初始化（时序见 spec §6：TX 先于 RX 启动、时钟最后释放）
 * ------------------------------------------------------------------ */

/* TX DMA 句柄(.ioc 无 DAC DMA, 手工创建), stm32g4xx_it.c 中 extern 引用 */
DMA_HandleTypeDef hdma_dac1_ch1;
DMA_HandleTypeDef hdma_dac1_ch2;
DMA_HandleTypeDef hdma_dac4_ch1;
DMA_HandleTypeDef hdma_dac4_ch2;

/* 创建 1 路 TX DMA: 内存→外设、循环, DMAMUX 请求挂 DAC 通道。
 * 外设侧宽度必须 WORD: G4 的 DAC DHR 寄存器只支持 32 位写, 半字/字节写会
 * 直接触发 DMA 总线传输错误 TEIF(上板实测: 4 路通道首拍全部报错停摆)。
 * 内存侧保持 HALFWORD, DMA 自动零扩展打包为 32 位写, DHR 仅取低 12 位。
 * (F1/F4 家族 DHR 可半字写, 网上大量例程为旧家族写法, 不可照搬) */
static void tx_dma_create(DMA_HandleTypeDef *hdma, DMA_Channel_TypeDef *ch, uint32_t request)
{
    hdma->Instance                 = ch;
    hdma->Init.Request             = request;
    hdma->Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma->Init.MemInc              = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma->Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma->Init.Mode                = DMA_CIRCULAR;
    hdma->Init.Priority            = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(hdma) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ------------------------------------------------------------------
 * 点阵直出诊断模式(无 DMA 版, 2026-08-15):
 * 1 = 屏蔽 ADC/DMA 管线, TIM3 更新中断 ≈30kHz 节拍步进 ad_da_pat_x/y,
 *     软件直写 DAC DHR(不经 DMA、不经触发链)。
 * 背景: 8-14 点阵经 DMA 通路输出时双轴接振镜即死机(未解决); 诊断阶段①②
 * (无 DMA 直写)双轴稳定。本模式在无 DMA 路径续做对照: 原始点阵 383 点
 * 线性缩放至 2048±400(生成脚本 tests/gen_pattern_2048pm400.py, 锚点校验:
 * 0→1648, 1024→1848, 2048→2048, 3071→2248, 4095→2448)。
 * 用途: 干净码流(无采样噪声)振动判别 + 双轴死机对照(vs 点阵 DMA 模式)。
 * 安全(8-14 复位事故教训——延迟启动触发时钟必须先把触发改 NONE): 本模式
 * DAC 触发配置即为 NONE, 写 DHR 立即锁入 DOR, 上电即输出 2048 中点,
 * 无"等触发"0V 满刻度窗口, TIM3 中断任何时候启动都安全。
 * ⚠️ JP3 必须断开(两源叠加禁令, spec §3.2)。FB 通道输出 0(不进内部环)。
 * ⚠️ 改回 0U 恢复 AD-DA 跟随通路后, 勿用 EIDE 默认 level-0 构建上板:
 *    基线管线须 ≥-O2(spec §9.12 定配结论, level-0 构建上板冻结)。
 * 模式开关 AD_DA_PATTERN_DIRECT_ENABLE 已移至文件头统一管理。 */

static const uint16_t ad_da_pat_x[AD_DA_PATTERN_LEN] = {
     1648, 1651, 1653, 1656, 1659, 1662, 1665, 1667, 1670, 1673, 1676, 1678, 1681, 1684, 1687, 1690,
     1692, 1695, 1698, 1701, 1703, 1706, 1709, 1712, 1715, 1717, 1720, 1723, 1725, 1728, 1731, 1733,
     1736, 1739, 1741, 1744, 1747, 1749, 1752, 1755, 1757, 1760, 1763, 1765, 1768, 1771, 1773, 1776,
     1779, 1781, 1784, 1787, 1789, 1792, 1795, 1797, 1800, 1803, 1805, 1808, 1811, 1813, 1816, 1819,
     1821, 1824, 1827, 1829, 1832, 1835, 1837, 1840, 1843, 1845, 1848, 1848, 1848, 1848, 1848, 1848,
     1848, 1851, 1854, 1856, 1859, 1862, 1865, 1867, 1870, 1873, 1876, 1879, 1881, 1884, 1887, 1890,
     1892, 1895, 1898, 1901, 1904, 1906, 1909, 1912, 1915, 1917, 1920, 1923, 1925, 1928, 1931, 1933,
     1936, 1939, 1941, 1944, 1947, 1949, 1952, 1955, 1957, 1960, 1963, 1965, 1968, 1971, 1973, 1976,
     1979, 1981, 1984, 1987, 1989, 1992, 1995, 1997, 2000, 2003, 2005, 2008, 2011, 2013, 2016, 2019,
     2021, 2024, 2027, 2029, 2032, 2035, 2037, 2040, 2043, 2045, 2048, 2048, 2048, 2048, 2048, 2048,
     2048, 2051, 2054, 2056, 2059, 2062, 2065, 2067, 2070, 2073, 2076, 2079, 2082, 2084, 2087, 2090,
     2092, 2095, 2098, 2101, 2104, 2106, 2109, 2112, 2115, 2117, 2120, 2123, 2125, 2128, 2131, 2133,
     2136, 2139, 2141, 2144, 2147, 2149, 2152, 2155, 2157, 2160, 2163, 2165, 2168, 2171, 2173, 2176,
     2179, 2181, 2184, 2187, 2190, 2192, 2195, 2198, 2201, 2204, 2206, 2209, 2212, 2215, 2217, 2220,
     2223, 2226, 2229, 2231, 2234, 2237, 2240, 2242, 2245, 2248, 2248, 2248, 2248, 2248, 2248, 2248,
     2251, 2253, 2256, 2259, 2261, 2264, 2267, 2269, 2272, 2275, 2277, 2280, 2283, 2285, 2288, 2291,
     2293, 2296, 2299, 2301, 2304, 2307, 2309, 2312, 2315, 2317, 2320, 2323, 2326, 2328, 2331, 2333,
     2336, 2339, 2341, 2344, 2347, 2349, 2352, 2355, 2357, 2360, 2363, 2365, 2368, 2371, 2373, 2376,
     2379, 2381, 2384, 2387, 2389, 2392, 2395, 2397, 2400, 2403, 2405, 2408, 2411, 2413, 2416, 2419,
     2421, 2424, 2427, 2429, 2432, 2435, 2437, 2440, 2443, 2445, 2448, 2448, 2448, 2448, 2448, 2448,
     2448, 2448, 2432, 2417, 2401, 2385, 2370, 2354, 2339, 2323, 2307, 2292, 2276, 2260, 2245, 2229,
     2214, 2198, 2182, 2167, 2151, 2135, 2120, 2104, 2089, 2073, 2057, 2042, 2026, 2010, 1995, 1979,
     1964, 1948, 1932, 1917, 1901, 1885, 1870, 1854, 1838, 1823, 1807, 1792, 1776, 1760, 1745, 1729,
     1713, 1698, 1682, 1667, 1659, 1655, 1653, 1652, 1651, 1650, 1649, 1649, 1649, 1649, 1648
};

static const uint16_t ad_da_pat_y[AD_DA_PATTERN_LEN] = {
     2181, 2178, 2174, 2170, 2166, 2163, 2159, 2155, 2152, 2148, 2144, 2141, 2137, 2133, 2130, 2126,
     2122, 2118, 2115, 2111, 2107, 2104, 2100, 2096, 2092, 2089, 2085, 2082, 2078, 2075, 2071, 2068,
     2064, 2060, 2057, 2053, 2050, 2046, 2043, 2039, 2036, 2032, 2028, 2025, 2021, 2018, 2014, 2011,
     2007, 2004, 2000, 1997, 1993, 1989, 1986, 1982, 1979, 1975, 1972, 1968, 1964, 1961, 1957, 1954,
     1950, 1947, 1943, 1939, 1936, 1932, 1929, 1925, 1922, 1918, 1915, 1915, 1915, 1915, 1915, 1915,
     1915, 1918, 1922, 1926, 1930, 1933, 1937, 1941, 1944, 1948, 1952, 1955, 1959, 1963, 1966, 1970,
     1974, 1978, 1981, 1985, 1989, 1992, 1996, 2000, 2004, 2007, 2011, 2014, 2018, 2021, 2025, 2028,
     2032, 2036, 2039, 2043, 2046, 2050, 2053, 2057, 2060, 2064, 2068, 2071, 2075, 2078, 2082, 2085,
     2089, 2092, 2096, 2099, 2103, 2107, 2110, 2114, 2117, 2121, 2124, 2128, 2132, 2135, 2139, 2142,
     2146, 2149, 2153, 2157, 2160, 2164, 2167, 2171, 2174, 2178, 2181, 2181, 2181, 2181, 2181, 2181,
     2181, 2178, 2174, 2170, 2166, 2163, 2159, 2155, 2152, 2148, 2144, 2141, 2137, 2133, 2130, 2126,
     2122, 2118, 2115, 2111, 2107, 2104, 2100, 2096, 2092, 2089, 2085, 2082, 2078, 2075, 2071, 2068,
     2064, 2060, 2057, 2053, 2050, 2046, 2043, 2039, 2036, 2032, 2028, 2025, 2021, 2018, 2014, 2011,
     2007, 2004, 2000, 1996, 1992, 1989, 1985, 1981, 1978, 1974, 1970, 1966, 1963, 1959, 1955, 1952,
     1948, 1944, 1941, 1937, 1933, 1930, 1926, 1922, 1918, 1915, 1915, 1915, 1915, 1915, 1915, 1915,
     1918, 1922, 1925, 1929, 1932, 1936, 1939, 1943, 1947, 1950, 1954, 1957, 1961, 1964, 1968, 1972,
     1975, 1979, 1982, 1986, 1989, 1993, 1997, 2000, 2004, 2007, 2011, 2014, 2018, 2021, 2025, 2028,
     2032, 2036, 2039, 2043, 2046, 2050, 2053, 2057, 2060, 2064, 2068, 2071, 2075, 2078, 2082, 2085,
     2089, 2092, 2096, 2099, 2103, 2107, 2110, 2114, 2117, 2121, 2124, 2128, 2132, 2135, 2139, 2142,
     2146, 2149, 2153, 2157, 2160, 2164, 2167, 2171, 2174, 2178, 2181, 2181, 2181, 2181, 2181, 2181,
     2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181,
     2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181,
     2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181,
     2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181, 2181
};

static volatile uint16_t ad_da_pat_idx = 0U;  /* 步进索引, TIM3 ISR 独享访问, 无锁 */

/* 快照前 64 样本排序取中值: 离群统计 rebase 基准, 避免 base 抓到冲激(如 255)
   致 out 全判离群。选择排序原地排快照前缀, 仅主循环调用, 不进 ISR。 */
static uint16_t msb_median(void)
{
    uint16_t n = 64U;
    for (uint16_t i = 0U; i < n - 1U; i++)
    {
        uint16_t m = i;
        for (uint16_t j = i + 1U; j < n; j++)
        {
            if (msb_snap[j] < msb_snap[m])
            {
                m = j;
            }
        }
        if (m != i)
        {
            uint16_t t      = msb_snap[i];
            msb_snap[i]     = msb_snap[m];
            msb_snap[m]     = t;
        }
    }
    return msb_snap[n / 2U];
}

/*  初始化函数  */
void AD_DA_Init(void)
{
    DAC_ChannelConfTypeDef sConfig = {0};

#if AD_DA_PATTERN_DIRECT_ENABLE
    /* 点阵直出诊断模式: 全屏蔽 ADC/TIM3 TRGO/DMA/管线 ISR, DAC 触发 NONE——
       写 DHR 立即锁入 DOR(无"等触发"0V 窗口, 初始化时刻即输出 2048 中点)。
       波形由 TIM3 更新中断 ≈30kHz 逐点直写(HAL_TIM_PeriodElapsedCallback)。 */
    sConfig.DAC_HighFrequency           = DAC_HIGH_FREQUENCY_INTERFACE_MODE_ABOVE_160MHZ;
    sConfig.DAC_DMADoubleDataMode       = DISABLE;
    sConfig.DAC_SignedFormat            = DISABLE;
    sConfig.DAC_SampleAndHold           = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger                 = DAC_TRIGGER_NONE;
    sConfig.DAC_Trigger2                = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer            = DAC_OUTPUTBUFFER_DISABLE;   /* DAC1: 直接出脚(与基线一致) */
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
    sConfig.DAC_UserTrimming            = DAC_TRIMMING_FACTORY;
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;   /* DAC4: 片内进 OPAMP4/5 */
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_2) != HAL_OK) Error_Handler();

    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac4, DAC_CHANNEL_1)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac4, DAC_CHANNEL_2)   != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Start(&hopamp4)              != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Start(&hopamp5)              != HAL_OK) Error_Handler();

    /* 上电初值: IN 2048 中点、FB 0(触发 NONE, 写即锁入, 振镜安全) */
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0)    != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0)    != HAL_OK) Error_Handler();

    /* TIM3 更新中断 = 点率节拍(回调直写 DHR, 见 HAL_TIM_PeriodElapsedCallback)。
       MX_TIM3_Init 已设 PSC=2-1, 此处仅改 ARR: 85-1 → 2833-1 ≈ 30.0035kHz。
       优先级 1; 单次中断仅两次寄存器直写(亚 µs 级), 33µs 周期内空隙充足,
       USB 枚举与主循环不受影响(诊断阶段③ 20kHz 同构已验证)。 */
    __HAL_TIM_SET_AUTORELOAD(&htim3, AD_DA_PATTERN_ARR - 1U);
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
    HAL_NVIC_SetPriority(TIM3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);
    if (HAL_TIM_Base_Start(&htim3) != HAL_OK) Error_Handler();

    LOG_SYS_INFO("[AD-DA] 点阵直出(无DMA): 383点 2048±400, 点率≈30.0035kHz 帧率≈78.3Hz, ADC/DMA已屏蔽");
    return;
#endif /* AD_DA_PATTERN_DIRECT_ENABLE */

    /* 1. 4 路 RX ADC 偏移校准(沿用原流程) */
    ADC_Offset_Calibration(&hadc2);
    ADC_Offset_Calibration(&hadc3);
    ADC_Offset_Calibration(&hadc4);
    ADC_Offset_Calibration(&hadc5);
    HAL_Delay(10);

    /* 2. 运行时重配 DAC 触发 = TIM3 TRGO(缓冲保持 OFF, 不动 .ioc/生成代码) */
    sConfig.DAC_HighFrequency           = DAC_HIGH_FREQUENCY_INTERFACE_MODE_ABOVE_160MHZ;
    sConfig.DAC_DMADoubleDataMode       = DISABLE;
    sConfig.DAC_SignedFormat            = DISABLE;
    sConfig.DAC_SampleAndHold           = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger                 = DAC_TRIGGER_T3_TRGO;
    sConfig.DAC_Trigger2                = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer            = DAC_OUTPUTBUFFER_DISABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;   /* DAC1: 直接出脚 */
    sConfig.DAC_UserTrimming            = DAC_TRIMMING_FACTORY;
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;   /* DAC4: 片内进 OPAMP4/5 */
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_2) != HAL_OK) Error_Handler();

#if MSB_TEST_ENABLE
    /* 被测轴 DAC 改触发 NONE: ISR 直写立即锁入 DOR, 与 1MHz 网格解耦 */
    sConfig.DAC_Trigger  = DAC_TRIGGER_NONE;
    sConfig.DAC_Trigger2 = DAC_TRIGGER_NONE;
#if MSB_TESTING_X
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
#else
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
#endif
#endif

    /* 3. 先使能输出端 */
    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac4, DAC_CHANNEL_1)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac4, DAC_CHANNEL_2)   != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Start(&hopamp4)              != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Start(&hopamp5)              != HAL_OK) Error_Handler();

    /* 4. 初值 + tx_buf 整体预填(防前半块垃圾值):
          IN 通道(CH1) 2048 中点; FB 通道(CH2) 0 —— 静息 0V, 与算法/原仓库
          DAC_FBX_SET(0) 意图一致(spec §9.7) */
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0)    != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0)    != HAL_OK) Error_Handler();
    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        const uint16_t idle_val = ((ch == 1U) || (ch == 3U)) ? 0U : 2048U;
        for (uint16_t i = 0; i < AD_DA_BLOCK; i++)
        {
            tx_buf[ch][i] = idle_val;
        }
    }

    /* 5. 手工创建 4 路 TX DMA(DMA1_Ch5–Ch8), 链接到 DAC 句柄。
       被测轴 DHR 由 TIM6 ISR 独享, 不建 TX DMA(spec §8 竞争警告) */
#if !MSB_TESTING_X
    tx_dma_create(&hdma_dac1_ch1, DMA1_Channel5, DMA_REQUEST_DAC1_CHANNEL1);
    __HAL_LINKDMA(&hdac1, DMA_Handle1, hdma_dac1_ch1);
#endif
#if !MSB_TESTING_Y
    tx_dma_create(&hdma_dac4_ch1, DMA1_Channel7, DMA_REQUEST_DAC4_CHANNEL1);
    __HAL_LINKDMA(&hdac4, DMA_Handle1, hdma_dac4_ch1);
#endif
    tx_dma_create(&hdma_dac1_ch2, DMA1_Channel6, DMA_REQUEST_DAC1_CHANNEL2);
    tx_dma_create(&hdma_dac4_ch2, DMA1_Channel8, DMA_REQUEST_DAC4_CHANNEL2);
    __HAL_LINKDMA(&hdac1, DMA_Handle2, hdma_dac1_ch2);
    __HAL_LINKDMA(&hdac4, DMA_Handle2, hdma_dac4_ch2);

    /* TX NVIC: 优先级 1(低于 RX 节拍中断 0, 避免抢占块处理), 仅处理传输错误 */
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 1, 0);
    HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 1, 0);
    HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 1, 0);
    HAL_NVIC_SetPriority(DMA1_Channel8_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel8_IRQn);

    /* 启动 4 路 TX DMA(循环、长度 AD_DA_BLOCK), 启动后即消费预填值。
       被测轴不启动 TX DMA(DHR 由 ISR 直写) */
#if !MSB_TESTING_X
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)tx_buf[0], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
#endif
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, (uint32_t *)tx_buf[1], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
#if !MSB_TESTING_Y
    if (HAL_DAC_Start_DMA(&hdac4, DAC_CHANNEL_1, (uint32_t *)tx_buf[2], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
#endif
    if (HAL_DAC_Start_DMA(&hdac4, DAC_CHANNEL_2, (uint32_t *)tx_buf[3], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();

    /* DAC 欠载兜底中断: HAL_DAC_Start_DMA 已使能 DMAUDRIE, 此处释放 NVIC。
       TIM6_DAC 线挂 DAC1&DAC3、TIM7_DAC 线挂 DAC2&DAC4; 优先级 2
       (低于 RX 节拍 0 与 TX 传输错误 1), handler 见 stm32g4xx_it.c:
       清欠载标志并继续运行, 严禁落入 Default_Handler 死锁 */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 2, 0);
    HAL_NVIC_SetPriority(TIM7_DAC_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    HAL_NVIC_EnableIRQ(TIM7_DAC_IRQn);

    /* 循环 DMA 每 64µs 置位 HT/TC, 本设计 TX 侧无需块中断:
       屏蔽 HT/TC、仅保留 TE(传输错误), 避免 ~12.5万次/秒 空中断(见 Global Constraints) */
#if !MSB_TESTING_X   /* 被测轴无 TX DMA 句柄, 跳过寄存器访问 */
    __HAL_DMA_DISABLE_IT(&hdma_dac1_ch1, DMA_IT_HT | DMA_IT_TC);
#endif
    __HAL_DMA_DISABLE_IT(&hdma_dac1_ch2, DMA_IT_HT | DMA_IT_TC);
#if !MSB_TESTING_Y
    __HAL_DMA_DISABLE_IT(&hdma_dac4_ch1, DMA_IT_HT | DMA_IT_TC);
#endif
    __HAL_DMA_DISABLE_IT(&hdma_dac4_ch2, DMA_IT_HT | DMA_IT_TC);

    /* 6. RX DMA 改指 rx_buf 并启动; 使能 hadc3 DMA 中断(块处理节拍, 优先级已在 dma.c 设 0) */
#if ADC_SOLO_CH4_ENABLE
    /* 判定模式: 仅 ADC4 转换(errata §2.7.9 互扰排除), 节拍改挂 ADC4; vx/ix/iy 缓冲停更 */
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    if (HAL_ADC_Start_DMA(&hadc4, (uint32_t *)rx_buf[2], AD_DA_BLOCK) != HAL_OK) Error_Handler();
#elif (ADC_SOLO == 3U)
    /* 判定模式: 仅 ADC3 转换——ADC4 不采样, 示波器看 U1A(Y) pin1 平台是否消失 */
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    if (HAL_ADC_Start_DMA(&hadc3, (uint32_t *)rx_buf[0], AD_DA_BLOCK) != HAL_OK) Error_Handler();
#elif (ADC_SOLO == 255U)
    /* 判定模式: 全部 ADC 停采——MCU 侧零采样, 示波器看 U1A(Y) pin1 平台是否消失 */
#else
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    if (HAL_ADC_Start_DMA(&hadc3, (uint32_t *)rx_buf[0], AD_DA_BLOCK) != HAL_OK) Error_Handler();
    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)rx_buf[1], AD_DA_BLOCK) != HAL_OK) Error_Handler();
    if (HAL_ADC_Start_DMA(&hadc4, (uint32_t *)rx_buf[2], AD_DA_BLOCK) != HAL_OK) Error_Handler();
    if (HAL_ADC_Start_DMA(&hadc5, (uint32_t *)rx_buf[3], AD_DA_BLOCK) != HAL_OK) Error_Handler();
#endif

    /* 7. 唯一时钟源最后释放: 收发同拍锁相 */
    if (HAL_TIM_Base_Start(&htim3) != HAL_OK) Error_Handler();

#if MSB_TEST_ENABLE
    /* 8. TIM6 ≈30.0035kHz 步进 major-carry 序列: 2833 tick@85MHz = 33.329µs,
       与 1µs 采样网格非整倍数 → 相位游走, 等效扫描边沿后 settling 过程 */
    __HAL_RCC_TIM6_CLK_ENABLE();
    htim6.Instance               = TIM6;
    htim6.Init.Prescaler         = 2 - 1;
    htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim6.Init.Period            = 2833 - 1;
    htim6.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
    {
        Error_Handler();
    }
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);
    if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK)
    {
        Error_Handler();
    }
    LOG_SYS_INFO("[MSB] 对照测试使能: 轴=%s, A/B/C 每 2s 轮转, 示波器看 DAC 引脚",
                 (MSB_TEST_AXIS == MSB_TEST_AXIS_X) ? "X" : "Y");
#endif
}


void ad5290_set_init(void)
{
  	AD5290_Init();
	HAL_Delay(10);
}


void param_init(void)
{
	if (bsp_flash_load(&flash_store) == 0)
	{
		radc_value  = flash_store.radc;
		comp_value  = flash_store.comp;
		LOG_SYS_INFO("load param from flash");
	}
	else
	{
		radc_value.x1 = 40;
		radc_value.x2 = 80;
		radc_value.x3 = 45;
		radc_value.y1 = 40;
		radc_value.y2 = 80;
		radc_value.y3 = 45;

		comp_value.x  = -80;
		comp_value.y  = -80;
		LOG_SYS_INFO("load param from default");
	}

	AD5290_SetAllCode((const uint8_t *)&radc_value);
	LOG_SYS_INFO("param: x1 = %04d, x2 = %04d, x3 = %04d, y1 = %04d, y2 = %04d, y3 = %04d", 
					radc_value.x1, radc_value.x2, radc_value.x3, radc_value.y1, radc_value.y2, radc_value.y3);
	LOG_SYS_INFO("comp: x = %04d, y = %04d", comp_value.x, comp_value.y);
	LOG_SYS_INFO("===================================================");

}


void lsnet_init(void)
{
	ls_app_init();
}


/*    滑动平均滤波    */
#define ADC_FILTER_SIZE 4
static uint16_t adc_value_filtered[ADC_FILTER_SIZE] = {0};
static uint8_t adc_value_filtered_index = 0;
uint16_t adc_filter(uint16_t value)
{
	adc_value_filtered[adc_value_filtered_index] = value;
	adc_value_filtered_index = (adc_value_filtered_index + 1) % ADC_FILTER_SIZE;
	uint32_t sum = 0;
	for(uint8_t i = 0; i < ADC_FILTER_SIZE; i++)
	{
		sum += adc_value_filtered[i];
	}
	return sum / ADC_FILTER_SIZE;
}

/*****************************************************/

/*  定时器任务  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim)
{
	static uint8_t count = 0;
	if(htim->Instance == TIM2)
	{
		/* 遗留死代码(spec §8 警示: TIM2 中断不得再启用, 与 TX DMA 竞争 DHR) */
		count = !count;
		if(count)
		{
			DAC_INX_SET(4095-2000);
		}
		else
		{
			DAC_INX_SET(0+2000);
		}
	}
#if MSB_TEST_ENABLE
	else if (htim->Instance == TIM6)
	{
		/* major-carry 序列步进: 触发 NONE, 写 DHR 立即锁入 DOR */
		msb_phase ^= 1U;
#if MSB_TESTING_X
		DAC1->DHR12R1 = msb_code[msb_sel][msb_phase];
#else
		DAC4->DHR12R1 = msb_code[msb_sel][msb_phase];
#endif
	}
#endif
#if AD_DA_PATTERN_DIRECT_ENABLE
	else if (htim->Instance == TIM3)
	{
		/* 点阵直出步进(无 DMA): 双轴同拍直写 DHR, 索引回绕。
		   TIM3 中断仅在 AD_DA_PATTERN_DIRECT_ENABLE=1 时使能, 跟随通路不进此分支 */
		uint16_t i = ad_da_pat_idx;

		DAC_INX_SET(ad_da_pat_x[i]);            /* X → DAC1_CH1 → PA4 */
		DAC_INY_SET(ad_da_pat_y[i]);            /* Y → DAC4_CH1 → OPAMP4 → PB12 */
		i++;
		if (i >= AD_DA_PATTERN_LEN)
		{
			i = 0U;
		}
		ad_da_pat_idx = i;
	}
#endif
}



/* MSB 测试主循环处理: 对照组轮转 + 窗口统计打印 + 电流快照 dump。
 * 每组 2s: 先打印上一组 min/max/离群数, 再切换并翻转 LED_X 作示波器同步标记。
 * 判读: 示波器看 DAC 引脚毛刺能量 A vs B/C; 串口 out 数为数字侧旁证。 */
void msb_test_poll(void)
{
    static uint32_t t_switch;
    static uint32_t t_dump;
    uint32_t        now = HAL_GetTick();

    if (now - t_switch >= MSB_SWITCH_MS)
    {
        t_switch += MSB_SWITCH_MS;
#if !MSB_VOFA_MODE
        LOG_SYS_INFO("[MSB] sel=%u (%u/%u) 结束: min=%u max=%u out=%lu",
                     msb_sel, msb_code[msb_sel][0], msb_code[msb_sel][1],
                     msb_min, msb_max, (unsigned long)msb_outlier);
#endif
        msb_sel     = (uint8_t)((msb_sel + 1U) % 3U);
        msb_min     = 4095U;
        msb_max     = 0U;
        msb_outlier = 0U;
        msb_base    = 0U;   /* 新窗口暂停离群统计, 等中值就位 */
        HAL_GPIO_TogglePin(LED_X_GPIO_Port, LED_X_Pin);   /* 示波器同步标记 */
#if !MSB_VOFA_MODE
        LOG_SYS_INFO("[MSB] 切换 sel=%u (%u/%u)", msb_sel,
                     msb_code[msb_sel][0], msb_code[msb_sel][1]);
#endif
    }

    if (msb_snap_ready != 0U)
    {
        msb_snap_ready = 0U;
        msb_base       = msb_median();   /* 本窗快照原地排序取中值, 作下窗离群基准 */
#if !MSB_VOFA_MODE
        if (now - t_dump >= 250U)
        {
            t_dump = now;
            for (uint16_t r = 0; r < MSB_SNAP_LEN; r += 16U)
            {
                const uint16_t *p = &msb_snap[r];
                printf("%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                       p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                       p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
            }
        }
#endif
    }
#if MSB_VOFA_MODE
    (void)t_dump;
#endif
}

/* file end */