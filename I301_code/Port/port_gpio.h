/* port_gpio.h —— PORT 契约: 引脚写原语
 * 语义: 按板型头(Board/)定义的平台无关引脚 ID 写电平; 不承诺读回。
 *   port_pin_write_multi 为同步多脚写(位带总线时钟沿等), 同端口时实现
 *   应合并为单次寄存器写, 保证多路沿相位一致。
 * 实现: Platform/<芯片>/port_gpio_*.c(ID→物理口/脚映射表)。 */
#ifndef __PORT_GPIO_H__
#define __PORT_GPIO_H__

#include <stdint.h>

typedef uint16_t port_pin_t;   /* 取值见板型头 board_pin_e */

void port_pin_write(port_pin_t pin, uint8_t level);
void port_pin_write_multi(const port_pin_t pins[], uint8_t n, uint8_t level);

#endif /* __PORT_GPIO_H__ */
