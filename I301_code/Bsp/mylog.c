/* mylog.c —— 日志框架实现(等级过滤 + 时间戳 + hex 转储)
 * 职责: log_output(等级标签 + 模块标签 + 时间戳)、log_output_hex
 *   (每行 16 字节, snprintf 拼接)。
 * 上下游: printf 走串口控制台; LOG_ENABLE=0 整体编译裁剪。 */
#include "mylog.h"
#include <stdarg.h>

/* 日志等级字符串 */
static const char *log_level_str[] = {
    "ERRO",
    "WARN",
    "INFO",
    "DBUG",
    "TRC "
};

/* =================== 时间戳实现 =================== */
/* 默认用 HAL_GetTick，你也可以换成 xTaskGetTickCount */
uint32_t log_get_tick(void)
{
    return HAL_GetTick();
}


/* =================== 日志输出实现 =================== */

#if LOG_ENABLE

void log_output(log_level_e level,
                const char *module,
                const char *fmt, ...)
{
    if (level > LOG_LEVEL)
    {
        return;
    }

    va_list args;

#if LOG_USE_TIMESTAMP
    {
        uint32_t tick = log_get_tick();   /* ms */

        uint32_t ms   = tick % 1000;
        uint32_t sec  = (tick / 1000) % 60;
        uint32_t min  = (tick / 60000);

        printf("[%03u:%03u:%03u]", min, sec, ms);
    }
#endif

    printf("[%s][%s] ", log_level_str[level], module);

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\r\n");
}

#endif /* LOG_ENABLE */


/*
    * 十六进制数据输出实现
    * level: 日志等级
    * module: 模块名称
    * title: 数据标题
    * buf: 数据缓冲区
    * len: 数据长度
*/
#if LOG_ENABLE

void log_output_hex(log_level_e level,
                    const char *module,
                    const char *title,
                    const uint8_t *buf,
                    uint16_t len)
{
    if (level > LOG_LEVEL)
    {
        return;
    }

    if (buf == NULL)
    {
        log_output(level, module, "%s: <null>", title ? title : "hex");
        return;
    }

    if (len == 0)
    {
        log_output(level, module, "%s: <empty>", title ? title : "hex");
        return;
    }

    if (title != NULL)
    {
        log_output(level, module, "%s, len=%u", title, len);
    }

    for (uint16_t offset = 0; offset < len; offset += 16)
    {
        char line[80];
        int pos;
        uint16_t chunk = len - offset;

        if (chunk > 16)
        {
            chunk = 16;
        }

        pos = snprintf(line, sizeof(line), "%04X: ", offset);

        for (uint16_t i = 0; i < chunk && pos > 0 && pos < (int)sizeof(line); i++)
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