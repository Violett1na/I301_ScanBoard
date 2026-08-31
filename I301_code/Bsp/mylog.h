/* mylog.h —— 编译期裁剪日志框架接口(I301 振镜 XY 板)
 * 职责: 日志等级与模块开关宏(编译期裁剪)、log_output/log_output_hex
 *   核心接口、LOG_* 快捷宏。
 * 上下游: mylog.c 实现; 全仓引用; 时间戳经 log_get_tick
 *   (默认 HAL_GetTick, 可换 RTOS)。 */
#ifndef __MYLOG_H__
#define __MYLOG_H__

#include "main.h"
#include <stdio.h>
#include <stdint.h>

/* 日志总开关 */
#define LOG_ENABLE                1

#define LOG_ENABLE_SYS            1
#define LOG_ENABLE_NET            0
#define LOG_ENABLE_LSNET          0
#define LOG_ENABLE_DMX            0
#define LOG_ENABLE_MENU           0
#define LOG_ENABLE_TEST           0
#define LOG_ENABLE_LASER_INFO     0
#define LOG_ENABLE_LASER_DEBUG    0



/* 日志等级控制（编译期） */
#define LOG_LEVEL           LOG_LEVEL_DEBUG

/* 是否打印时间戳 */
#define LOG_USE_TIMESTAMP   1

/* 时间戳函数（HAL / RTOS 都可替换） */
uint32_t log_get_tick(void);

/* =================== 日志等级 =================== */
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE,
} log_level_e;


/* =================== 核心打印接口 =================== */

#if LOG_ENABLE

void log_output(log_level_e level,
                const char *module,
                const char *fmt, ...);

void log_output_hex(log_level_e level,
    const char *module,
    const char *title,
    const uint8_t *buf,
    uint16_t len);

#else
#define log_output(...)
#define log_output_hex(...)
#endif

/* =================== 快捷宏 =================== */

#define LOG_ERROR(mod, fmt, ...) log_output(LOG_LEVEL_ERROR, mod, fmt, ##__VA_ARGS__)
#define LOG_WARN(mod, fmt, ...) log_output(LOG_LEVEL_WARN,  mod, fmt, ##__VA_ARGS__)
#define LOG_INFO(mod, fmt, ...) log_output(LOG_LEVEL_INFO,  mod, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(mod, fmt, ...) log_output(LOG_LEVEL_DEBUG, mod, fmt, ##__VA_ARGS__)
#define LOG_TRACE(mod, fmt, ...) log_output(LOG_LEVEL_TRACE, mod, fmt, ##__VA_ARGS__)
#define LOG_HEX(level, mod, title, buf, len) log_output_hex(level, mod, title, buf, len)
/* =================== 模块化宏（可按需添加） =================== */
#if LOG_ENABLE_SYS
#define LOG_SYS_INFO(fmt, ...) LOG_INFO("SYS  ", fmt, ##__VA_ARGS__)
#define LOG_SYS_ERROR(fmt, ...) LOG_ERROR("SYS  ", fmt, ##__VA_ARGS__)
#define LOG_SYS_HEX(title, buf, len) LOG_HEX(LOG_LEVEL_INFO, "SYS  ", title, buf, len)
#else
#define LOG_SYS_INFO(...)
#define LOG_SYS_ERROR(...)
#define LOG_SYS_HEX(...)
#endif

#if LOG_ENABLE_NET
#define LOG_NET_INFO(fmt, ...) LOG_INFO("NET  ", fmt, ##__VA_ARGS__)
#define LOG_NET_ERROR(fmt, ...) LOG_ERROR("NET  ", fmt, ##__VA_ARGS__)
#define LOG_NET_HEX(title, buf, len) LOG_HEX(LOG_LEVEL_INFO, "NET  ", title, buf, len)
#else
#define LOG_NET_INFO(...)
#define LOG_NET_ERROR(...)
#define LOG_NET_HEX(...)
#endif

#if LOG_ENABLE_LSNET
#define LOG_LSNET_INFO(fmt, ...) LOG_INFO("LSNET", fmt, ##__VA_ARGS__)
#define LOG_LSNET_ERROR(fmt, ...) LOG_ERROR("LSNET", fmt, ##__VA_ARGS__)
#define LOG_LSNET_HEX(title, buf, len) LOG_HEX(LOG_LEVEL_INFO, "LSNET", title, buf, len)
#else
#define LOG_LSNET_INFO(...)
#define LOG_LSNET_ERROR(...)
#define LOG_LSNET_HEX(...)
#endif

#if LOG_ENABLE_DMX
#define LOG_DMX_INFO(fmt, ...) LOG_INFO("DMX  ", fmt, ##__VA_ARGS__)
#define LOG_DMX_DEBUG(fmt, ...) LOG_DEBUG("DMX  ", fmt, ##__VA_ARGS__)
#define LOG_DMX_HEX(title, buf, len) LOG_HEX(LOG_LEVEL_INFO, "DMX  ", title, buf, len)
#else
#define LOG_DMX_INFO(...)
#define LOG_DMX_DEBUG(...)
#define LOG_DMX_HEX(...)
#endif

#if LOG_ENABLE_TEST
#define LOG_TEST_INFO(fmt, ...) LOG_INFO("TEST ", fmt, ##__VA_ARGS__)
#define LOG_TEST_HEX(title, buf, len) LOG_HEX(LOG_LEVEL_INFO, "TEST ", title, buf, len)
#else
#define LOG_TEST_INFO(...)
#define LOG_TEST_HEX(...)
#endif

#if LOG_ENABLE_LASER_INFO
#define LOG_LASER_INFO(fmt, ...) LOG_INFO("LASER", fmt, ##__VA_ARGS__)
#define LOG_LASER_ERROR(fmt, ...) LOG_ERROR("LASER", fmt, ##__VA_ARGS__)
#define LOG_LASER_HEX(title, buf, len) LOG_HEX(LOG_LEVEL_INFO, "LASER", title, buf, len)

#else
#define LOG_LASER_INFO(...)
#define LOG_LASER_ERROR(...)
#define LOG_LASER_HEX(...)
#endif
#if LOG_ENABLE_LASER_DEBUG
#define LOG_LASER_DEBUG(fmt, ...) LOG_DEBUG("LASER", fmt, ##__VA_ARGS__)
#else
#define LOG_LASER_DEBUG(...)
#endif


#if LOG_ENABLE_MENU
#define LOG_MENU_INFO(fmt, ...) LOG_INFO("MENU", fmt, ##__VA_ARGS__)
#else
#define LOG_MENU_INFO(...)
#endif

#endif
