/* mylog.c —— 日志框架实现(等级过滤 + 时间戳 + hex 转储)
 * 职责: log_output(等级标签 + 模块标签 + 时间戳)、log_output_hex
 *   (每行 16 字节, 行内拼接后整行输出)。
 * 2026-09-02 分层重构: 时间戳经 port_tick, 输出经 port_console,
 *   不再依赖 printf/HAL(Core 中 printf 重定向保留作生成层备用通道)。 */
#include "mylog.h"
#include "port_tick.h"
#include "port_console.h"
#include <stdarg.h>
#include <stdio.h>

/* 日志等级字符串 */
static const char *log_level_str[] = {
    "ERRO",
    "WARN",
    "INFO",
    "DBUG",
    "TRC "
};

/* 单条日志行缓冲上限(截断保护; 现网最长为 param 初始化与 hex 行) */
#define LOG_LINE_MAX 160U

/* =================== 日志输出实现 =================== */

#if LOG_ENABLE

void log_output(log_level_e level,
                const char *module,
                const char *fmt, ...)
{
    char    line[LOG_LINE_MAX];
    int     pos = 0;
    va_list args;

    if (level > LOG_LEVEL)
    {
        return;
    }

#if LOG_USE_TIMESTAMP
    {
        uint32_t tick = port_tick_ms();   /* ms */
        uint32_t ms   = tick % 1000U;
        uint32_t sec  = (tick / 1000U) % 60U;
        uint32_t min  = tick / 60000U;

        pos += snprintf(&line[pos], sizeof(line) - (size_t)pos,
                        "[%03u:%03u:%03u]",
                        (unsigned)min, (unsigned)sec, (unsigned)ms);
    }
#endif

    if (pos < (int)sizeof(line))
    {
        pos += snprintf(&line[pos], sizeof(line) - (size_t)pos,
                        "[%s][%s] ", log_level_str[level], module);
    }

    if (pos < (int)sizeof(line))
    {
        va_start(args, fmt);
        pos += vsnprintf(&line[pos], sizeof(line) - (size_t)pos, fmt, args);
        va_end(args);
    }

    if (pos < (int)sizeof(line))
    {
        pos += snprintf(&line[pos], sizeof(line) - (size_t)pos, "\r\n");
    }

    /* snprintf 族返回"应写入"长度, 可能超缓冲: 钳位后输出 */
    if (pos > (int)sizeof(line))
    {
        pos = (int)sizeof(line);
    }
    port_console_write(line, (uint32_t)pos);
}

#endif /* LOG_ENABLE */


/*
    * 十六进制数据输出实现
    * level: 日志等级
    * module: 模块名称
    * title: 数据标题
    * buf: 缓冲区
    * len: 长度
*/
#if LOG_ENABLE

void log_output_hex(log_level_e level,
                    const char *module,
                    const char *title,
                    const uint8_t *buf,
                    uint16_t len)
{
    uint16_t offset;

    if (level > LOG_LEVEL)
    {
        return;
    }

    if (buf == NULL)
    {
        log_output(level, module, "%s: <null>", title ? title : "hex");
        return;
    }

    if (len == 0U)
    {
        log_output(level, module, "%s: <empty>", title ? title : "hex");
        return;
    }

    if (title != NULL)
    {
        log_output(level, module, "%s, len=%u", title, len);
    }

    for (offset = 0U; offset < len; offset += 16U)
    {
        char line[80];
        int pos;
        uint16_t chunk = (uint16_t)(len - offset);
        uint16_t i;

        if (chunk > 16U)
        {
            chunk = 16U;
        }

        pos = snprintf(line, sizeof(line), "%04X: ", offset);

        for (i = 0U; (i < chunk) && (pos > 0) && (pos < (int)sizeof(line)); i++)
        {
            pos += snprintf(&line[pos],
                            sizeof(line) - (size_t)pos,
                            "%02X ",
                            buf[offset + i]);
        }

        log_output(level, module, "%s", line);
    }
}

#endif /* LOG_ENABLE */
