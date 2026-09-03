/* port_sys_g4.c —— STM32G4 实现: 系统动作 */
#include "port_sys.h"
#include "main.h"

void port_sys_reset(void)
{
    NVIC_SystemReset();   /* CMSIS-core, 等效原 __NVIC_SystemReset */
}
