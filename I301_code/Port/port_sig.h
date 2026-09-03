/* port_sig.h —— PORT 契约: 过流信令硬件动作
 * 语义: 两路常开方波(1kHz/50%, 上电即跑、永不启停)的建立(含反馈通路
 *   让路与引脚复用), 以及 4053 反馈出口选择脚的切换。检测与决策在
 *   APP 层(ocd_sig 状态机), 本契约只做动作。
 * 实现: Platform/<芯片>/port_sig_*.c(硬件再配置清单随实现文件)。 */
#ifndef __PORT_SIG_H__
#define __PORT_SIG_H__

#include <stdint.h>

#define PORT_SIG_AXIS_X  0U
#define PORT_SIG_AXIS_Y  1U

void port_sig_init(void);   /* 建立两路常开方波; 之后不再启停 */

/* 4053 选择脚: on=1 该轴 FB 出口切信令方波, on=0 回模拟直通 */
void port_sig_switch_set(uint8_t axis, uint8_t on);

#endif /* __PORT_SIG_H__ */
