/* bsp_flash.c —— 通用参数持久化实现(魔术字 + CRC-16 双重校验)
 * 2026-09-02 分层重构: 擦/写/读改经 port_flash 契约(平台无关);
 * 接口泛化为不透明 blob, 不再依赖 App 层类型。
 * 上下游: param.c 调用 save/load; 本层只做完整性校验,
 * 数值范围一致性检查由调用方(param_init)承担。
 * 注意: 本布局(较旧版新增长度字段)与历史存储不兼容, 重构后首次
 * 上电将以"无有效数据"回退默认参数(一次性)。 */
#include "bsp_flash.h"
#include "port_flash.h"
#include "mylog.h"
#include <stddef.h>
#include <string.h>

#define BSP_FLASH_MAGIC  0x4C535059U   /* "LSPY" */

#pragma pack(1)
/* 存储总结构(本层私有): 魔术字 + 长度 + 数据 + CRC */
typedef struct
{
    uint32_t magic;                        /* 魔术字校验 */
    uint16_t len;                          /* 存入时数据长度 */
    uint8_t  data[BSP_FLASH_DATA_MAX];     /* 用户数据 */
    uint16_t crc;                          /* CRC-16 校验 */
} flash_param_t;
#pragma pack()

/* CRC-16 校验（与协议层保持一致的简易实现） */
static uint16_t calc_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;
    uint8_t  j;

    for (i = 0U; i < len; i++)
    {
        crc ^= (uint16_t)data[i];
        for (j = 0U; j < 8U; j++)
        {
            if (crc & 0x0001)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            }
            else
            {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

int bsp_flash_save(const void *data, uint16_t len)
{
    flash_param_t param;

    if ((data == NULL) || (len == 0U) || (len > BSP_FLASH_DATA_MAX))
    {
        return -1;
    }

    memset(&param, 0xFF, sizeof(param));
    param.magic = BSP_FLASH_MAGIC;
    param.len   = len;
    memcpy(param.data, data, len);
    param.crc = calc_crc16((const uint8_t *)&param, offsetof(flash_param_t, crc));

    if (port_flash_erase() != 0)
    {
        LOG_SYS_ERROR("flash erase failed");
        return -1;
    }

    if (port_flash_write((const uint8_t *)&param, sizeof(param)) != 0)
    {
        LOG_SYS_ERROR("flash write failed");
        return -1;
    }

    LOG_SYS_INFO("flash save ok.");
    return 0;
}

int bsp_flash_load(void *data, uint16_t len)
{
    flash_param_t param;

    if ((data == NULL) || (len == 0U) || (len > BSP_FLASH_DATA_MAX))
    {
        return -1;
    }

    if (port_flash_read((uint8_t *)&param, sizeof(param)) != 0)
    {
        return -1;
    }

    /* 校验魔术字 */
    if (param.magic != BSP_FLASH_MAGIC)
    {
        LOG_SYS_ERROR("flash param magic invalid.");
        return -1;
    }

    /* 校验 CRC */
    if (calc_crc16((const uint8_t *)&param, offsetof(flash_param_t, crc)) != param.crc)
    {
        LOG_SYS_ERROR("flash param crc invalid.");
        return -1;
    }

    /* 长度一致性: 布局变更/混烧防护 */
    if (param.len != len)
    {
        LOG_SYS_ERROR("flash param len mismatch.");
        return -1;
    }

    memcpy(data, param.data, len);
    LOG_SYS_INFO("flash load ok.");
    return 0;
}

/* file end */
