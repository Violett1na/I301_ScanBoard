#ifndef LS_PROTO_RECEIVE_H
#define LS_PROTO_RECEIVE_H

#include "ls_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ls_cb_reset_device_t)(void);

typedef void (*ls_cb_on_reply_t)(ls_base_reply_t *reply);

typedef void (*ls_cb_on_ctrl_reply_t)(uint16_t typeCMD);

/* 控制数字电位器包回调：返回 0 表示设置成功，非 0 表示失败 */
typedef int  (*ls_cb_ctrl_rdac_t)(const ls_ctrl_rdac_t *rdac);

/* 设置补偿值包回调：返回 0 表示设置成功，非 0 表示失败 */
typedef int  (*ls_cb_ctrl_set_comp_t)(const ls_ctrl_set_comp_t *comp);

/* 参数保存包回调：返回 0 表示保存成功，非 0 表示失败 */
typedef int  (*ls_cb_ctrl_save_param_t)(void);

/* -----------------------------------------------------------------------
 * 回调函数集合结构体
 * 应用层填充此结构体后传给 ls_receiver_init_callbacks()
 * ----------------------------------------------------------------------- */
typedef struct
{
    /**  设备  **/
    ls_cb_reset_device_t      reset_device;      /* 重启包：重启设备 */
    ls_cb_ctrl_rdac_t         ctrl_rdac;         /* 控制包：设置数字电位器 */
    ls_cb_ctrl_set_comp_t     ctrl_set_comp;     /* 控制包：设置补偿值 */
    ls_cb_ctrl_save_param_t   ctrl_save_param;   /* 控制包：参数保存 */

    /**  主机  **/
    ls_cb_on_reply_t          on_reply;          /* 回复包：处理回复数据 */
    ls_cb_on_ctrl_reply_t     on_ctrl_reply;     /* 控制包：应答包处理 */
} ls_receive_callbacks_t;

int ls_parse(ls_packet_t *pkt, uint8_t *in_buf, uint16_t in_len);


void ls_receiver_init_callbacks(const ls_receive_callbacks_t *cbs);

int ls_handle(ls_packet_t *pkt);

/* 基础类 */
int handle_base_query(void);
int handle_base_reply(ls_packet_t *pkt);
int handle_base_reset_device(void);

/* 数据类 */
/* 控制类 */
int handle_ctrl_reply(ls_packet_t *pkt);
int handle_ctrl_rdac(ls_packet_t *pkt);
int handle_ctrl_set_comp(ls_packet_t *pkt);
int handle_ctrl_save_param(ls_packet_t *pkt);


#ifdef __cplusplus
}
#endif

#endif // LS_PROTO_RECEIVE_H