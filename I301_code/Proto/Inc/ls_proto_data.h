#ifndef LS_PROTO_DATA_H
#define LS_PROTO_DATA_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 2026-09-02 重构: 摘除 mylog.h 依赖(曾把 HAL 头链带入协议核心)。
 * LS_LOG_ENABLE=1 时需由使用方自备日志宏环境。 */

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
    LS_BASE_REPLY_ALL         = 0x0104,    /* 全量回复: 三套配置 + 工况 */

    LS_CTRL_REPLY             = 0x0301,
    LS_CTRL_RDAC              = 0x0302,    /* 控制数字电位器 */
    LS_CTRL_SET_COMP          = 0x0303,    /* 设置补偿值 */
    LS_CTRL_SAVE_PARAM        = 0x0304,    /* 参数保存 */
    LS_CTRL_SET_PROFILE       = 0x0305,    /* 整包写一套配置 */
    LS_CTRL_GET_ALL           = 0x0306,    /* 请求全量回读 */
    LS_CTRL_FORCE_PROFILE     = 0x0307,    /* 强制套 / 保活 / 解除 */

} ls_type_e;

/* 配置套: 三工况, 语义见 spec §3.1 */
#define LS_PROFILE_NUM   3
#define LS_PROFILE_NONE  0xFF   /* 强制解除(0x0307 专用) */

typedef enum
{
    LS_RADC_X = 0x01,
    LS_RADC_Y = 0x02,
} ls_radc_xy_e;

typedef enum
{
    LS_RADC_CH_1 = 0x01,
    LS_RADC_CH_2 = 0x02,
    LS_RADC_CH_3 = 0x03,
} ls_radc_ch_e;



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

    uint8_t  r_x1;
    uint8_t  r_x2;
    uint8_t  r_x3;

    uint8_t  r_y1;
    uint8_t  r_y2;
    uint8_t  r_y3;

    int16_t comp_x;
    int16_t comp_y;
}  ls_base_reply_t;

/* 控制类：数字电位器控制包结构（命令字 0x0302）
 *   xy   : 0x01=X 轴，0x02=Y 轴
 *   ch   : 0x01/0x02/0x03 选择该轴下 1/2/3 号通道
 *   code : 0~255，AD5290 RDAC 码值
 */
typedef struct
{
    uint8_t xy;
    uint8_t ch;
    uint8_t code;
} ls_ctrl_rdac_t;

/* 控制类：设置补偿值包结构（命令字 0x0303）
 *   xy    : 0x01=X 通道，0x02=Y 通道
 *   value : 补偿值，范围 [-2000, 2000]
 */
typedef struct
{
    uint8_t  xy;
    int16_t  value;
} ls_ctrl_set_comp_t;

/* 一套配置(0x0305 写、0x0104 读共用, 10B)
 * 分层: 协议层自定义, 不依赖 App 层 param.h 类型(规范 13-3);
 *   字节布局与 param_profile_t 一致, 映射在 device_app 层做。
 *   radc 单字节无需转换; comp_x/comp_y 需大小端转换。 */
typedef struct
{
    uint8_t  radc[6];      /* X1 X2 X3 Y1 Y2 Y3, 各 0~255 */
    int16_t  comp_x;       /* X 轴补偿值, [-2000, 2000] */
    int16_t  comp_y;       /* Y 轴补偿值, [-2000, 2000] */
} ls_profile_t;

/* 控制类: 整包写一套(命令字 0x0305)
 *   profile : 0~2 目标套; 越界拒收 */
typedef struct
{
    uint8_t      profile;
    ls_profile_t data;
} ls_ctrl_set_profile_t;

/* 控制类: 请求全量回读(命令字 0x0306)
 *   req : 填 0xFF */
typedef struct
{
    uint8_t req;
} ls_ctrl_get_all_t;

/* 控制类: 强制套(命令字 0x0307), 同时用作保活帧
 *   profile : 0~2 强制到该套; 0xFF 解除强制 */
typedef struct
{
    uint8_t profile;
} ls_ctrl_force_t;

/* 基础类: 全量回复(命令字 0x0104, 36B)
 *   active_profile : 设备当前生效套(自主判定结果)
 *   forced_profile : 强制套; 0xFF = 未强制 */
typedef struct
{
    uint16_t     device_id;
    uint16_t     device_version;
    uint8_t      active_profile;
    uint8_t      forced_profile;
    ls_profile_t profiles[LS_PROFILE_NUM];
} ls_all_reply_t;


#pragma pack()

#endif /* LS_PROTO_DATA_H */
