#include "ocd.h"
#include "port_tick.h"

/* ------------------------------------------------------------------
 * 软件过流检测实现(时序语义: 200ms 强制 + 1s 盲期, 用户选定)
 *
 * 时间线: ARMED ──(连续 peg >8)──→ HOLD ──(200ms)──→ BLANK ──(满1s)──→ ARMED
 * ⚠️ 本行为受 OCD_ENABLE 宏门控(见 ocd.h): 0 = 整体摘除(过流只发信令不动图像)。
 *   HOLD : X/Y IN 通道强制 2048、FB 强制 0, 斩断命令级过流;
 *   BLANK: 恢复跟随, 检测关闭防抖, 距跳闸满 OCD_CYCLE_MS 重新武装。
 * 全部逻辑跑在 1MHz 管线 ISR(算法槽包装), 每半块 16 样本, 开销为
 * 每样本几次整数比较, 远裕于 16µs 死线。
 * 2026-09-02 重构: 时基改经 port_tick 契约, 槽位经 set/get 接口。
 * ------------------------------------------------------------------ */

#if OCD_ENABLE

/* ---- 模块内部状态 ---- */
static ad_da_process_fn_t s_inner;                   /* 被包装的内层算法 */
static volatile uint8_t   s_state = OCD_STATE_ARMED; /* 状态机, 主循环经接口读 */
static volatile uint32_t  s_trips;                   /* 累计跳闸次数 */
static uint16_t           s_run;                     /* 当前连续 peg 长度, ISR 独享 */
static uint32_t           s_t_trip;                  /* 跳闸时刻 tick, ISR 独享 */

/* 安全值填充: IN 中点 2048、FB 静息 0(与管线上电预填安全态一致) */
static void ocd_fill_safe(uint16_t *const out[AD_DA_CH_NUM], uint16_t n)
{
    uint16_t *p_inx = out[0];
    uint16_t *p_fbx = out[1];
    uint16_t *p_iny = out[2];
    uint16_t *p_fby = out[3];

    while (n-- != 0U)
    {
        *p_inx++ = 2048U;   /* X 轴命令回中点, 线圈电流趋向 0 */
        *p_fbx++ = 0U;      /* X 轴 FB 静息 */
        *p_iny++ = 2048U;   /* Y 轴命令回中点 */
        *p_fby++ = 0U;      /* Y 轴 FB 静息 */
    }
}

/* 算法槽包装: 检测 + 状态机 + 强制输出, 其余委托内层算法 */
static void ocd_process(const uint16_t *in[AD_DA_CH_NUM],
                        uint16_t       *out[AD_DA_CH_NUM],
                        uint16_t        n)
{
    uint32_t now = port_tick_ms();

    /* 按时长推进状态: HOLD 满 200ms 释放跟随, BLANK 满 1s 重新武装 */
    if (s_state == OCD_STATE_HOLD)
    {
        if ((now - s_t_trip) >= OCD_HOLD_MS)
        {
            s_state = OCD_STATE_BLANK;
            s_run   = 0U;
        }
    }
    else if (s_state == OCD_STATE_BLANK)
    {
        if ((now - s_t_trip) >= OCD_CYCLE_MS)
        {
            s_state = OCD_STATE_ARMED;
        }
    }

    /* 检测: ix(ch1)/iy(ch3) 连续 peg(==0 或 ==4095), 超过阈值即跳闸 */
    if (s_state == OCD_STATE_ARMED)
    {
        const uint16_t *pix = in[1];
        const uint16_t *piy = in[3];
        uint16_t        i;

        for (i = 0U; i < n; i++)
        {
            uint16_t a = pix[i];
            uint16_t b = piy[i];
            if ((a == 0U) || (a == 4095U) || (b == 0U) || (b == 4095U))
            {
                s_run++;
                if (s_run > OCD_TRIP_RUN)
                {
                    s_state  = OCD_STATE_HOLD;   /* 跳闸: 本半块起强制安全值 */
                    s_t_trip = now;
                    s_trips++;
                    s_run    = 0U;
                    break;
                }
            }
            else
            {
                s_run = 0U;   /* 非 peg 样本清零连续计数 */
            }
        }
    }

    /* 输出: HOLD 期强制安全值, 否则委托内层算法 */
    if (s_state == OCD_STATE_HOLD)
    {
        ocd_fill_safe(out, n);
    }
    else
    {
        s_inner(in, out, n);
    }
}

/* 挂载: 包装算法槽。port_pipe_init 之后调用一次 */
void ocd_init(void)
{
    s_inner  = ad_da_process_fn_get();
    s_state  = OCD_STATE_ARMED;
    s_run    = 0U;
    s_t_trip = 0U;
    s_trips  = 0U;
    ad_da_process_fn_set(ocd_process);   /* 最后赋值: 此后管线经包装层 */
}

uint8_t ocd_state(void)
{
    return s_state;
}

uint32_t ocd_trip_count(void)
{
    return s_trips;
}

#else /* !OCD_ENABLE —— 编译期整体摘除图像干预, 空实现保调用点免改
         (与 ocd_sig.c 尾部回退模式一致)。信令链路由 ocd_sig 独立承担,
         算法槽链 = linear → ocd_sig。2026-08-28 用户决策, 见
         docs/superpowers/specs/2026-08-28-ocd-image-intervention-disable-design.md */

void ocd_init(void) {}
uint8_t ocd_state(void) { return OCD_STATE_ARMED; }   /* 未干预: 报告"武装态"供观测 */
uint32_t ocd_trip_count(void) { return 0U; }

#endif /* OCD_ENABLE */

/* file end */
