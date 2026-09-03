/* port_console_g4.c —— STM32G4 实现: USART1 轮询发送
 * 行为对齐原 printf 重定向(Core/Src/usart.c fputc): 等 TC 后置 TDR,
 * 逐字节阻塞。波特率等配置由生成代码完成, 本文件只搬运字节。 */
#include "port_console.h"
#include "main.h"

void port_console_write(const char *buf, uint32_t len)
{
    uint32_t i;

    if ((buf == NULL) || (len == 0U))
    {
        return;
    }

    for (i = 0U; i < len; i++)
    {
        while ((USART1->ISR & USART_ISR_TC_Msk) == 0U)
        {
        }
        USART1->TDR = (uint8_t)buf[i];
    }
}
