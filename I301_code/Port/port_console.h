/* port_console.h —— PORT 契约: 日志输出后端
 * 语义: 阻塞式字节流输出(日志语境, 非热路径); 字符格式由调用方完成,
 *   本契约只搬运。
 * 实现: Platform/<芯片>/port_console_*.c(现平台 = USART1 轮询)。 */
#ifndef __PORT_CONSOLE_H__
#define __PORT_CONSOLE_H__

#include <stdint.h>

void port_console_write(const char *buf, uint32_t len);

#endif /* __PORT_CONSOLE_H__ */
