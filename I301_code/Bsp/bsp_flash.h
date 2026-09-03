/* bsp_flash.h —— 通用参数持久化接口(DRV 层)
 * 职责: 参数区的完整性语义(魔术字 + CRC-16 + 长度一致性); 擦/写/读
 *   原语经 port_flash 契约, 平台无关。数据为不透明 blob, 本层不认识
 *   应用类型(2026-09-02 重构消除原对 param.h 的层倒挂依赖)。
 * 上下游: param.c 调用 save/load; 数值范围一致性检查由调用方承担。
 * 布局说明: 存储总结构(魔术字+长度+数据+CRC)为本层私有, 见 .c。 */
#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#include <stdint.h>

/* 持久化数据区最大长度(编译期约束, 调用方超限拒存;
 * 调用方应以静态检查固定, 见 param.c) */
#define BSP_FLASH_DATA_MAX  32U

/* 保存参数(擦除+写入, 魔术字+CRC): 0 成功, -1 失败 */
int bsp_flash_save(const void *data, uint16_t len);

/* 加载参数(魔术字+CRC+长度匹配校验): 0 成功, -1 无有效数据/长度不符 */
int bsp_flash_load(void *data, uint16_t len);

#endif /* __BSP_FLASH_H__ */
