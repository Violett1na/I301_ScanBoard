#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#include "main.h"
#include "param.h"    /* flash_store_t */

/* STM32G474RBT6 Flash 参数（128KB，单 Bank） */
#define FLASH_PAGE_SIZE          0x800U          /* 2KB */
#define FLASH_TOTAL_SIZE         0x20000U        /* 128KB */

/* 使用最后一页（Page 63）作为参数存储区 */
#define FLASH_PARAM_PAGE         63U
#define FLASH_PARAM_BANK         FLASH_BANK_1
#define FLASH_PARAM_ADDR         (FLASH_BASE + FLASH_TOTAL_SIZE - FLASH_PAGE_SIZE)

/* 魔术字，用于校验存储数据有效性 */
#define FLASH_PARAM_MAGIC        0x4C535059U     /* "LSPY" */

#pragma pack(1)
/* Flash 存储数据结构（内部使用，应用层无需关心） */
typedef struct
{
    uint32_t       magic;          /* 魔术字校验 */
    flash_store_t  store;          /* 用户数据 */
    uint16_t       crc;            /* CRC-16 校验 */
} flash_param_t;
#pragma pack()

int  bsp_flash_save(const flash_store_t *store); /* 保存参数(擦除+写入): 0 成功, -1 失败 */
int  bsp_flash_load(flash_store_t *store);       /* 加载参数(magic+CRC 校验): 0 成功, -1 无有效数据 */

#endif /* __BSP_FLASH_H__ */
