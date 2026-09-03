/* ad_da_alg.c —— AD-DA 默认线性算法(平台无关纯模块)
 * 2026-09-02 分层重构: 自原 App/ad_da.c 拆出, 宿主可测
 * (测试见 tests/ad_da_alg)。
 * 饱和钳位说明: 原实现用 M4 硬件饱和指令(单指令完成 0..4095 双向钳位),
 * 属 CMSIS-core 特性、且随 HAL 头链引入; 为保持本模块平台无关与宿主
 * 可测, 改为等价整数钳位(-O2 下编译器对饱和模式可映射为硬件指令)。
 * 若上板复测全链周期超出预算(预算见平台实现的块长警示), 允许按
 * 白名单程序(设计规范 §6.3)恢复硬件饱和指令并在此注明。
 *
 * 性能要点(承自原实现, 死线 = 半块窗口, 现配置 16µs):
 * 1) off 预合并为 base, 每样本只剩一次加/减法;
 * 2) 指针步进代替二维下标;
 * 3) 循环二重展开, 减半循环记账开销;
 * 4) 反相/同相分支在样本循环之外(每通道判定一次, 无逐样本判断)。
 * 全链(本函数+信令包装+节拍分发)原实测 ≈1200-1400 周期, 预算 2720;
 * 优化等级必须 ≥ -O2(见 eide.yml; level-0 必冻结)。 */
#include "ad_da.h"
#include "param.h"

/* 12bit 饱和钳位(0..4095) */
static inline uint16_t ad_da_sat12(int32_t v)
{
    if (v < 0)
    {
        return 0U;
    }
    if (v > 4095)
    {
        return 4095U;
    }
    return (uint16_t)v;
}

/* 单通道线性内核(IN 反相 / FB 同相, 饱和钳位),
 * inv: 0 = FB 同相, 非 0 = IN 反相(调用方按通道号 0/2 与 1/3 判定)。 */
static void ad_da_chan_linear(const uint16_t *p_in,
                              uint16_t       *p_out,
                              uint16_t        cnt,
                              int32_t         b,
                              uint8_t         inv)
{
    if (inv != 0U)                    /* IN 通道: 反相 */
    {
        while (cnt >= 2U)             /* 二重展开 */
        {
            p_out[0] = ad_da_sat12(b - (int32_t)p_in[0]);
            p_out[1] = ad_da_sat12(b - (int32_t)p_in[1]);
            p_in  += 2U;
            p_out += 2U;
            cnt   -= 2U;
        }
        if (cnt != 0U)                /* 奇数尾巴(n=16 不会走到, 保通用性) */
        {
            *p_out = ad_da_sat12(b - (int32_t)*p_in);
        }
    }
    else                              /* FB 通道: 同相 */
    {
        while (cnt >= 2U)             /* 二重展开 */
        {
            p_out[0] = ad_da_sat12(b + (int32_t)p_in[0]);
            p_out[1] = ad_da_sat12(b + (int32_t)p_in[1]);
            p_in  += 2U;
            p_out += 2U;
            cnt   -= 2U;
        }
        if (cnt != 0U)                /* 奇数尾巴(n=16 不会走到, 保通用性) */
        {
            *p_out = ad_da_sat12(b + (int32_t)*p_in);
        }
    }
}

/* IN/FB 通道分制线性算法:
 *   IN 通道: y = (4095 + off) - x
 *     —— 符号分析见 spec §1: 抵消 ADC-V 调理反相, 进 U1C 求和点端到端符号为正;
 *   FB 通道: y = off + x
 *     —— 反馈导出要求(2026-08-06 上板确认, spec §9.7): 静息 0V、有激励时出波形,
 *     不反相; 静息 i≈0 ⇒ y=off 钳到 0; 原统一反相公式会把 FB 静息点顶到 ≈2.2V。
 *   off 取协议补偿值(私有于 param.c: 主循环经 setter 写、ISR 经
 *   param_comp() 只读指针读, 对齐加载天然原子, 不加锁)
 * 刻度: 1 LSB ≈ 0.61mV(DAC端) ≈ 1.22mV(模拟求和点, 经 U15 ×2)；
 *   off 为偏置叠加(调零), 非校准 */
void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM],
                          uint16_t       *out[AD_DA_CH_NUM],
                          uint16_t        n)
{
    int32_t base[AD_DA_CH_NUM];
    uint8_t inv[AD_DA_CH_NUM];
    uint8_t ch;

    const volatile comp_value_t *comp = param_comp();   /* ISR 只读, 直读字段零开销 */

    /* IN 通道: y = (4095 + off) - x */
    base[0] = 4095 + (int32_t)comp->x;   inv[0] = 1U;   /* vx → DA_INX */
    base[2] = 4095 + (int32_t)comp->y;   inv[2] = 1U;   /* vy → DA_INY */
    /* FB 通道: y = off + x */
    base[1] = (int32_t)comp->x;          inv[1] = 0U;   /* ix → DA_FBX */
    base[3] = (int32_t)comp->y;          inv[3] = 0U;   /* iy → DA_FBY */

    for (ch = 0U; ch < AD_DA_CH_NUM; ch++)
    {
        ad_da_chan_linear(in[ch], out[ch], n, base[ch], inv[ch]);
    }
}

/* file end */
