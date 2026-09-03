/* ocd_sig.c — 过流 PWM 信令固件模块(检测包装层)
 * spec: docs/superpowers/specs/2026-08-27-ocd-pwm-signaling-design.md
 *
 * 职责:
 *   1. 算法槽最外层包装(链: linear→ocd→ocd_sig): 按轴独立检测
 *      (纯单元见 ocd_sig.h), 跳闸/释放经 port_sig 契约切 4053 选择脚,
 *      不填样本、不碰 out[];
 *   2. 初始化时经 port_sig_init 建立两路常开方波(通路让路/引脚复用/
 *      PWM 时基等硬件再配置清单见平台实现 port_sig_g4.c 文件头)。
 *
 * 2026-09-02 分层重构: 原固件壳(再配置清单①~⑤)下沉
 *   Platform/stm32g4/port_sig_g4.c; 时基改经 port_tick;
 *   4053 切换改经 port_sig_switch_set(引脚定义见板型头)。
 *
 * Fail-safe: 4053 选择脚默认低 = 模拟直通; MCU 死机信令丢失但
 *   反馈链路保持现状(与 ocd 同一固有边界, spec §8)。
 * 时序: 计时经 port_tick 契约(ISR 内读, 误差 ≤1ms), 同 ocd 惯例。 */

#include "ocd_sig.h"
#include "port_pipe.h"   /* ad_da_process_fn_t / AD_DA_CH_NUM / 槽位接口 */
#include "port_sig.h"    /* port_sig_init / port_sig_switch_set */
#include "port_tick.h"   /* port_tick_ms */

#if OCD_SIG_ENABLE

/* ---- 模块内部状态 ---- */
static ad_da_process_fn_t s_inner;      /* 被包装的内层算法(= ocd 包装层) */
static ocd_sig_axis_t     s_axis_x;     /* X 轴状态机, 检测源 ix = in[1] */
static ocd_sig_axis_t     s_axis_y;     /* Y 轴状态机, 检测源 iy = in[3] */

/* ---- 算法槽最外层包装: 逐样本检测 + 只切 4053 选择脚,
 *      out[] 无条件委托内层(本模块不改任何通道输出) ---- */
static void ocd_sig_process(const uint16_t *in[AD_DA_CH_NUM],
                            uint16_t       *out[AD_DA_CH_NUM],
                            uint16_t        n)
{
    uint32_t        now = port_tick_ms();
    const uint16_t *pix = in[1];   /* ix */
    const uint16_t *piy = in[3];   /* iy */
    uint16_t        i;

    for (i = 0U; i < n; i++)
    {
        uint8_t evx = ocd_sig_feed(&s_axis_x, pix[i], now);
        uint8_t evy = ocd_sig_feed(&s_axis_y, piy[i], now);

        if (evx == OCD_SIG_EV_TRIP)
        {
            port_sig_switch_set(PORT_SIG_AXIS_X, 1U);
        }
        else if (evx == OCD_SIG_EV_RELEASE)
        {
            port_sig_switch_set(PORT_SIG_AXIS_X, 0U);
        }

        if (evy == OCD_SIG_EV_TRIP)
        {
            port_sig_switch_set(PORT_SIG_AXIS_Y, 1U);
        }
        else if (evy == OCD_SIG_EV_RELEASE)
        {
            port_sig_switch_set(PORT_SIG_AXIS_Y, 0U);
        }
    }

    s_inner(in, out, n);   /* 透传: 检测与输出完全解耦 */
}

/* ---- 挂载: main.c 中 ocd_init() 之后调用一次 ----
 * 顺序: 轴状态 → 信令硬件建立(含通路让路, 须在管线启动之后) →
 * 包装算法槽(最后赋值)。 */
void ocd_sig_init(void)
{
    ocd_sig_axis_init(&s_axis_x);
    ocd_sig_axis_init(&s_axis_y);

    port_sig_init();

    s_inner          = ad_da_process_fn_get();
    ad_da_process_fn_set(ocd_sig_process);   /* 最后赋值: 此后管线经包装层 */
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
