/* bsp_flash.c —— Flash 末页参数存储（magic + CRC-16 双重校验）
 * 上下游: param_init/协议保存句柄调用 save/load; 本层只做完整性校验,
 * 数值范围一致性检查由调用方(param_init)承担。 */
#include "bsp_flash.h"
#include "mylog.h"
#include <string.h>

/* CRC-16 校验（与协议层保持一致的简易实现） */
static uint16_t calc_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief 擦除参数存储页
 * @return 0 成功，-1 失败
 */
static int flash_erase_param_page(void)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0;

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks     = FLASH_PARAM_BANK;
    erase_init.Page      = FLASH_PARAM_PAGE;
    erase_init.NbPages   = 1;

    status = HAL_FLASHEx_Erase(&erase_init, &page_error);
    if (status != HAL_OK)
    {
        LOG_SYS_ERROR("flash erase failed, err: 0x%08X", (unsigned int)page_error);
        return -1;
    }
    return 0;
}

/**
 * @brief 将数据按双字（64bit）写入 Flash
 * @param addr  目标地址（必须 8 字节对齐）
 * @param data  源数据
 * @param len   数据长度（字节），内部自动补齐到 8 的倍数
 * @return 0 成功，-1 失败
 */
static int flash_write_data(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t dword_count = (len + 7) / 8;
    uint64_t dword       = 0xFFFFFFFFFFFFFFFFULL;

    for (uint16_t i = 0; i < dword_count; i++)
    {
        dword = 0xFFFFFFFFFFFFFFFFULL;
        uint16_t offset  = i * 8;
        uint16_t copy_len = (len - offset) >= 8 ? 8 : (len - offset);
        memcpy(&dword, &data[offset], copy_len);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + offset, dword) != HAL_OK)
        {
            LOG_SYS_ERROR("flash write failed at 0x%08X", (unsigned int)(addr + offset));
            return -1;
        }
    }
    return 0;
}

/**
 * @brief 保存参数到 Flash
 * @param store  指向需要保存的数据结构体
 * @return 0 成功，-1 失败
 */
int bsp_flash_save(const flash_store_t *store)
{
    if (store == NULL)
    {
        return -1;
    }

    flash_param_t param;
    memset(&param, 0xFF, sizeof(param));

    param.magic = FLASH_PARAM_MAGIC;
    memcpy(&param.store, store, sizeof(flash_store_t));
    param.crc = calc_crc16((const uint8_t *)&param, offsetof(flash_param_t, crc));

    HAL_FLASH_Unlock();

    int ret = flash_erase_param_page();
    if (ret < 0)
    {
        HAL_FLASH_Lock();
        return -1;
    }

    ret = flash_write_data(FLASH_PARAM_ADDR, (const uint8_t *)&param, sizeof(flash_param_t));

    HAL_FLASH_Lock();

    if (ret == 0)
    {
        LOG_SYS_INFO("flash save ok.");
    }
    return ret;
}

/**
 * @brief 从 Flash 加载参数
 * @param store  输出数据结构体
 * @return 0 成功（数据有效），-1 失败（无有效数据）
 */
int bsp_flash_load(flash_store_t *store)
{
    if (store == NULL)
    {
        return -1;
    }

    const flash_param_t *p = (const flash_param_t *)FLASH_PARAM_ADDR;

    /* 校验魔术字 */
    if (p->magic != FLASH_PARAM_MAGIC)
    {
        LOG_SYS_ERROR("flash param magic invalid.");
        return -1;
    }

    /* 校验 CRC */
    uint16_t crc = calc_crc16((const uint8_t *)p, offsetof(flash_param_t, crc));
    if (crc != p->crc)
    {
        LOG_SYS_ERROR("flash param crc invalid.");
        return -1;
    }

    memcpy(store, &p->store, sizeof(flash_store_t));

    LOG_SYS_INFO("flash load ok.");
    return 0;
}
