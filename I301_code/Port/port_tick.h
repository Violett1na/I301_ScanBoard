/* port_tick.h —— PORT 契约: 时基与延时原语
 * 语义: 毫秒时基(单调, 允许回绕)、忙等延时(毫秒/周期级)、系统时钟频率。
 * 实现: Platform/<芯片>/port_tick_*.c。
 * 消费者: ocd/ocd_sig 时序、mylog 时间戳、param 电位器上电延时、
 *   ad5290 位带时序换算。 */
#ifndef __PORT_TICK_H__
#define __PORT_TICK_H__

#include <stdint.h>

uint32_t port_tick_ms(void);            /* 毫秒时基 */
void     port_delay_ms(uint32_t ms);    /* 忙等延时(毫秒) */
void     port_delay_cycles(uint32_t n); /* 忙等延时(周期级, 位带时序用) */
uint32_t port_sysclk_hz(void);          /* 系统时钟频率(Hz) */

#endif /* __PORT_TICK_H__ */
