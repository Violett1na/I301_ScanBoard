#ifndef LS_PROTO_DATA_H
#define LS_PROTO_DATA_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mylog.h"

#define LS_ENDIAN_ENABLE    1                  /* 发送端大小端互转使能（默认大端则关闭） */
#define LS_RX_ENDIAN_ENABLE 1                  /* 接收端大小端互转使能（默认大端则关闭） */

#define LS_HEADER_STR    "LIGHTSPACE-XY"       /* 协议头字符串 扫描板专属 */
#define LS_HEADER_LEN    13

#define LS_VERSION       0x0001                /* 协议版本 */

#define LS_DATA_BASE_LEN 23                    /* 协议包中除数据内容以外的基础长度 */
#define LS_MAX_DATA_LEN  1024*5                /* 协议包中数据内容的最大长度 */

#define LS_LOG_ENABLE 0                        /* 日志开关 */
#if LS_LOG_ENABLE

#ifdef __cplusplus
extern "C" {
#endif
void ls_log_info(const char *fmt, ...);      // 供 C++ 外部调用的日志函数
#ifdef __cplusplus
}
#endif
#define LS_LOG_INFO(fmt, ...)  LOG_INFO("SYS  ", fmt, ##__VA_ARGS__)    //根据情况修改这个log宏

#else
#define LS_LOG_INFO(fmt, ...)
#endif

#pragma pack(1)

typedef enum
{
    LS_BASE_QUERY             = 0x0101,
    LS_BASE_REPLY             = 0x0102,
    LS_BASE_RESET_DEVICE      = 0x0103,

    LS_CTRL_REPLY             = 0x0301,

} ls_type_t;


/* 协议包数据结构 */
typedef struct
{
    char     header[LS_HEADER_LEN];
    uint16_t pck_len;
    
    uint16_t version;
    uint8_t  type;
    uint8_t  cmd;
    uint16_t data_len;

    uint8_t data[LS_MAX_DATA_LEN - LS_DATA_BASE_LEN];  

    uint16_t crc;
} ls_packet_t;

/* 基础类：回复包结构 */
typedef struct
{
    uint16_t device_id;
    uint16_t device_version;


}  ls_base_reply_t;


#pragma pack()

#endif /* LS_PROTO_DATA_H */
