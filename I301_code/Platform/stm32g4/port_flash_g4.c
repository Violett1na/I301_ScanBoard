/* port_flash_g4.c —— STM32G4 实现: 参数区擦/写/读
 * G4 特性: 编程最小单位 64bit 双字、页 2KB、单 Bank; 参数区 = 末页。
 * 行为承自原 bsp_flash.c(逻辑不变, 厂商依赖收敛至此);
 * 擦/写各自独立完成解锁-上锁(原为一次解锁覆盖擦+写, 电气等效)。 */
#include "port_flash.h"
#include "chip_cfg.h"
#include "main.h"
#include <string.h>

#define PARAM_ADDR  (FLASH_BASE + CHIP_CFG_FLASH_TOTAL_SIZE \
                     - CHIP_CFG_FLASH_PAGE_SIZE)

int port_flash_erase(void)
{
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0U;

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks     = FLASH_BANK_1;
    erase_init.Page      = CHIP_CFG_FLASH_PARAM_PAGE;
    erase_init.NbPages   = 1U;

    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }
    HAL_FLASH_Lock();
    return 0;
}

int port_flash_write(const uint8_t *data, uint16_t len)
{
    uint16_t dword_count;
    uint64_t dword;
    uint16_t i;

    if (data == NULL)
    {
        return -1;
    }

    dword_count = (uint16_t)((len + 7U) / 8U);

    HAL_FLASH_Unlock();
    for (i = 0U; i < dword_count; i++)
    {
        uint16_t offset   = (uint16_t)(i * 8U);
        uint16_t copy_len = ((len - offset) >= 8U) ? 8U
                                                   : (uint16_t)(len - offset);

        dword = 0xFFFFFFFFFFFFFFFFULL;   /* 尾部不足双字处保持擦除态 */
        memcpy(&dword, &data[offset], copy_len);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              PARAM_ADDR + offset, dword) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();
    return 0;
}

int port_flash_read(uint8_t *out, uint16_t len)
{
    if (out == NULL)
    {
        return -1;
    }

    /* 参数区在片内 flash, 直接按地址读 */
    memcpy(out, (const void *)PARAM_ADDR, len);
    return 0;
}
