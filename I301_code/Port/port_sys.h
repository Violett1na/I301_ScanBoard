/* port_sys.h —— PORT 契约: 系统动作
 * 语义: 整机复位(协议复位命令用)。致命错误路径不属本契约,
 * 由启动代码(生成层)保留原语义。
 * 实现: Platform/<芯片>/port_sys_*.c。 */
#ifndef __PORT_SYS_H__
#define __PORT_SYS_H__

void port_sys_reset(void);

#endif /* __PORT_SYS_H__ */
