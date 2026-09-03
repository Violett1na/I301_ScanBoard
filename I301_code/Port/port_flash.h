/* port_flash.h —— PORT 契约: 参数区 flash 原语
 * 语义: "参数区"(位置/页大小/编程粒度)由平台 chip_cfg 决定, 契约只暴露
 *   擦/写/读; 布局语义(magic/CRC/长度)在 DRV 层 bsp_flash, 不属本契约。
 * 实现: Platform/<芯片>/port_flash_*.c(对齐与粒度差异由实现内部处理)。 */
#ifndef __PORT_FLASH_H__
#define __PORT_FLASH_H__

#include <stdint.h>

int port_flash_erase(void);                     /* 擦除参数区: 0 成功, -1 失败 */
int port_flash_write(const uint8_t *data, uint16_t len); /* 写参数区 */
int port_flash_read(uint8_t *out, uint16_t len);         /* 读参数区 */

#endif /* __PORT_FLASH_H__ */
