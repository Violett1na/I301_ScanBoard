#ifndef LS_PROTO_TRANS_H
#define LS_PROTO_TRANS_H


#include "ls_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ls_cb_send_t)(uint8_t *buf, uint16_t len);
typedef void (*ls_cb_get_device_info_t)(ls_base_reply_t *reply);


typedef struct
{
    ls_cb_send_t      send;           /* 必须注册：字节流发送函数     */

    ls_cb_get_device_info_t get_device_info; /* 查询包：获取设备信息 */

} ls_trans_callbacks_t;

extern ls_packet_t    s_work_pkt;
extern uint8_t        s_work_buf[LS_MAX_DATA_LEN];
extern uint16_t       s_work_len;

int s_pack_and_send(void);
void ls_trans_init_callbacks(const ls_trans_callbacks_t *cbs);

/* 基础类 */
int ls_base_query(void);
int ls_base_reset_device(void);
int ls_base_reply(void);

/* 数据类 */

/* 控制类 */
int ls_ctrl_reply(uint16_t typeCMD);
int ls_ctrl_rdac(ls_radc_xy_e xy, ls_radc_ch_e ch, uint8_t code);
int ls_ctrl_set_comp(ls_radc_xy_e xy, int16_t value);
int ls_ctrl_save_param(void);


#ifdef __cplusplus
}
#endif

#endif // LS_PROTO_TRANS_H