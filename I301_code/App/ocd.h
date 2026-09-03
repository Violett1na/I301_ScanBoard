#ifndef __OCD_H__
#define __OCD_H__

#include "port_pipe.h"

/* ------------------------------------------------------------------
 * 软件过流检测 (OCD, Over-Current Detection)
 *
 * 判据: ix(ch1) / iy(ch3) 出现连续 peg 码(==0 或 ==4095)超过
 *   OCD_TRIP_RUN 次即判定过流。连续计数可滤孤立采样毛刺。
 * 动作: 跳闸后 X/Y 轴 IN 通道(ch0/ch2)强制 2048 中点、FB 通道
 *   (ch1/ch3)强制 0(与管线上电预填安全态一致), 持续
 *   OCD_HOLD_MS; 随后恢复跟随, 检测空白至距跳闸 OCD_CYCLE_MS
 *   才重新武装(1s 防抖盲期, 用户选定语义)。
 *
 * 挂载: 算法槽包装——ocd_init() 把算法槽包一层,
 *   检测与强制都发生在 1MHz 管线 ISR 内(实时数据通路例外条款),
 *   跳闸延迟 ≤ 半块 16µs + 计数窗。时序经 port_tick 契约(ISR 内读,
 *   误差 ≤1ms)。
 *
 * 注意: 盲期内持续故障会驱动输出最长 (OCD_CYCLE_MS-OCD_HOLD_MS),
 *   硬件级保护仍靠 F1 保险丝/LM3886 内部限流, 本模块为命令级软保护。
 * ------------------------------------------------------------------ */
#define OCD_ENABLE     0U      /* 总开关: 0 = 编译期摘除图像干预(过流只发信令不动图像,
                                  2026-08-28 用户决策; 原 200ms 强置+1s 盲期行为见 #if 分支);
                                  1 = 恢复原行为(真过流联调对照时可切回) */
#define OCD_TRIP_RUN   8U      /* peg 连续次数阈值: >8 次跳闸 */
#define OCD_HOLD_MS    200U    /* 跳闸后强制安全值持续时长 */
#define OCD_CYCLE_MS   1000U   /* 跳闸后检测重新武装周期(盲期) */

/* 状态枚举(观测/日志用) */
#define OCD_STATE_ARMED 0U     /* 检测武装, 正常跟随 */
#define OCD_STATE_HOLD  1U     /* 过流强制安全值中 */
#define OCD_STATE_BLANK 2U     /* 已释放, 检测盲期内 */

void     ocd_init(void);       /* port_pipe_init 之后调用一次, 包装算法槽 */
uint8_t  ocd_state(void);      /* 当前状态, 主循环观测用 */
uint32_t ocd_trip_count(void); /* 累计跳闸次数 */

#endif /* __OCD_H__ */
