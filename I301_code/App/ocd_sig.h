#ifndef __OCD_SIG_H__
#define __OCD_SIG_H__

/* ------------------------------------------------------------------
 * 过流 PWM 信令 —— 纯状态机单元(无副作用依赖, 宿主可测, 测试见 tests/)
 * spec: docs/superpowers/specs/2026-08-27-ocd-pwm-signaling-design.md
 *
 * 职责: 按轴独立检测过流(连续 peg), 给出跳闸/释放事件; 固件层(ocd_sig.c)
 *   据此经 port_sig 契约切 4053 选择脚, 把该轴 FB 出口从模拟直通切到
 *   常开方波报总控板切激光。本单元不做波形、不碰输出缓冲;
 *   方波建立/引脚复用等硬件动作属 port_sig 契约(实现见平台层)。
 *
 * 判据(同 ocd.c 语义, 按轴独立):
 *   peg = 样本==0 或 ==4095(撞量程轨);
 *   SIG_OFF: 连续 peg 计数 > OCD_SIG_TRIP_RUN 跳闸, 非 peg 清零;
 *   SIG_ON:  peg 清零释放计时, 无 peg 稳定 OCD_SIG_RELEASE_MS 释放。
 * ------------------------------------------------------------------ */

#include <stdint.h>

/* ---- 参数表(编译期宏, 集中管理, 依据见 spec §6) ---- */
#define OCD_SIG_ENABLE      1U            /* 总开关: 0 = 编译期整体摘除(空实现回退) */
#define OCD_SIG_TRIP_RUN    8U            /* 跳闸阈值: 连续 peg >8(即第 9 个)跳闸 */
#define OCD_SIG_RELEASE_MS  1000U         /* 防抖释放窗: 无 peg 稳定满 1s 回模拟直通 */

/* ---- peg 判据(复用 ocd 语义: 撞量程轨) ---- */
#define OCD_SIG_IS_PEG(s)   (((s) == 0U) || ((s) == 4095U))

/* ---- ocd_sig_feed 返回事件 ---- */
#define OCD_SIG_EV_NONE     0U        /* 无状态迁移 */
#define OCD_SIG_EV_TRIP     1U        /* OFF->ON: 该轴 4053 切信令方波 */
#define OCD_SIG_EV_RELEASE  2U        /* ON->OFF: 该轴 4053 回模拟直通 */

/* ---- 按轴独立状态机 ---- */
typedef struct
{
    uint32_t trips;       /* 累计跳闸次数 */
    uint32_t t_clean;     /* 无 peg 稳定窗起点 tick(ms) */
    uint16_t run;         /* 当前连续 peg 长度, ISR 独享 */
    uint8_t  on;          /* 0=SIG_OFF(模拟直通) 1=SIG_ON(PWM 信令) */
    uint8_t  clean_armed; /* 释放计时激活标志, ISR 独享 */
} ocd_sig_axis_t;

static inline void ocd_sig_axis_init(ocd_sig_axis_t *ax)
{
    ax->trips       = 0U;
    ax->t_clean     = 0U;
    ax->run         = 0U;
    ax->on          = 0U;
    ax->clean_armed = 0U;
}

/* 喂入一个样本, 返回事件。
 * now_ms = port_tick_ms()(ISR 内读, 误差 ≤1ms, 对 1s 窗口无影响)。 */
static inline uint8_t ocd_sig_feed(ocd_sig_axis_t *ax, uint16_t sample, uint32_t now_ms)
{
    uint8_t peg = OCD_SIG_IS_PEG(sample) ? 1U : 0U;
    uint8_t ev  = OCD_SIG_EV_NONE;

    if (ax->on == 0U)
    {
        /* SIG_OFF: 连续 peg 计数, 超阈跳闸 */
        if (peg != 0U)
        {
            ax->run++;
            if (ax->run > OCD_SIG_TRIP_RUN)
            {
                ax->on          = 1U;
                ax->run         = 0U;
                ax->clean_armed = 0U;
                ax->trips++;
                ev = OCD_SIG_EV_TRIP;
            }
        }
        else
        {
            ax->run = 0U;   /* 非 peg 清零连续计数, 滤孤立毛刺 */
        }
    }
    else
    {
        /* SIG_ON: 防抖释放 —— peg 清零重计, 无 peg 稳定满窗释放 */
        if (peg != 0U)
        {
            ax->clean_armed = 0U;
        }
        else if (ax->clean_armed == 0U)
        {
            ax->clean_armed = 1U;
            ax->t_clean     = now_ms;
        }
        else if ((now_ms - ax->t_clean) >= OCD_SIG_RELEASE_MS)
        {
            ax->on          = 0U;
            ax->clean_armed = 0U;
            ev = OCD_SIG_EV_RELEASE;
        }
    }
    return ev;
}

/* ---- 固件 API(实现见 ocd_sig.c; 宿主单测只用上面的纯单元) ---- */
void     ocd_sig_init(void);        /* ocd_init() 之后调用一次 */
uint8_t  ocd_sig_state(void);       /* bit0 = X 轴 SIG_ON, bit1 = Y 轴 SIG_ON */
uint32_t ocd_sig_trip_count(void);  /* 累计跳闸次数(两轴合计, 观测用) */

#endif /* __OCD_SIG_H__ */
