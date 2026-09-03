/* chip_cfg.h —— STM32G474RBTx 芯片事实(Platform 配置轴)
 * 架构定位: 芯片级参数集中处, 供本平台 port_*.c 使用;
 * 上层(APP/DRV)不直接 include 本文件, 经 PORT 契约间接获得。
 * 换芯片 = 另建 Platform/<芯片>/ 目录, 重写本文件与全部 port_*.c。 */
#ifndef __CHIP_CFG_H__
#define __CHIP_CFG_H__

/* SystemClock_Config 结果: HSE 5MHz × PLL(68/5/2) = 170MHz */
#define CHIP_CFG_SYSCLK_HZ        170000000U

/* Flash 参数存储布局(参数区 = 单 Bank 末页) */
#define CHIP_CFG_FLASH_PAGE_SIZE  0x800U    /* 2KB */
#define CHIP_CFG_FLASH_TOTAL_SIZE 0x20000U  /* 128KB */
#define CHIP_CFG_FLASH_PARAM_PAGE 63U       /* Page 63 = 最后一页 */

#endif /* __CHIP_CFG_H__ */
