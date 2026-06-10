#include "ls_proto_trans.h"

/*  创建内部回调模块  */
static const ls_trans_callbacks_t *s_cbs = NULL;

ls_packet_t    s_work_pkt;                       // 所有需要发送的包都使用这个pkt
uint8_t        s_work_buf[LS_MAX_DATA_LEN];
uint16_t       s_work_len;


/* 检查 send 回调是否已注册，并在打包成功后执行发送 */
int s_pack_and_send(void)
{
    if (s_cbs == NULL || s_cbs->send == NULL)
    {
        LS_LOG_INFO("sender: send callback not registered.");
        return -2;
    }
    // LS_LOG_INFO("ls send successfully, len: %d", s_work_len);
    // for (int i = 0; i < s_work_len; i++)
    // {
    //     LS_LOG_INFO("%02X ", s_work_buf[i]);
    // }
    s_cbs->send(s_work_buf, s_work_len);
    return 0;
}

/*  注册回调函数  */
void ls_trans_init_callbacks(const ls_trans_callbacks_t *cbs)
{
    s_cbs = cbs;
}
/**************************************************************/
/*    主机处理发送函数    */
/**************************************************************/
/**
 * @brief 发送基础查询包，获取设备信息
 */
int ls_base_query(void)
{
    memset(&s_work_pkt, 0, sizeof(s_work_pkt));

    uint8_t query_infor;
    query_infor = 0xff;
    
    //确认类型和命令字
    s_work_pkt.type = LS_BASE_QUERY >> 8;    
    s_work_pkt.cmd  = LS_BASE_QUERY & 0xFF;         //基础查询包

    // 确认数据长度，拷贝数据内容
    s_work_pkt.data_len = sizeof(uint8_t);
    memcpy(s_work_pkt.data, &query_infor, sizeof(uint8_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;

    // 打包
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;    //打包失败
    }
    // 发送
    return s_pack_and_send();
}

/**
 * @brief 发送设备重启包，重启设备
*/
int ls_base_reset_device(void)
{
    memset(&s_work_pkt, 0, sizeof(s_work_pkt));

    uint8_t reset_infor;
    reset_infor = 0xff;
    
    //确认类型和命令字
    s_work_pkt.type = LS_BASE_RESET_DEVICE >> 8;    
    s_work_pkt.cmd  = LS_BASE_RESET_DEVICE & 0xFF;      

    // 确认数据长度，拷贝数据内容
    s_work_pkt.data_len = sizeof(uint8_t);
    memcpy(s_work_pkt.data, &reset_infor, sizeof(uint8_t));
    // 确认数据长度，拷贝数据内容
    s_work_pkt.data_len = sizeof(uint8_t);
    memcpy(s_work_pkt.data, &reset_infor, sizeof(uint8_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;

    // 打包
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;    //打包失败
    }
    // 发送
    return s_pack_and_send();
}

/**
 * @brief 发送控制数字电位器包(0x0302)
 * @param xy   0x01=X 轴，0x02=Y 轴
 * @param ch   0x01/0x02/0x03 选择该轴下 1/2/3 号通道
 * @param code 0~255 RDAC 码值
 * @return 0 成功，<0 失败
 *
 * 载荷三个字段全为单字节，无需大小端转换。
 */
int ls_ctrl_rdac(ls_radc_xy_e xy, ls_radc_ch_e ch, uint8_t code)
{
    memset(&s_work_pkt, 0, sizeof(s_work_pkt));

    ls_ctrl_rdac_t rdac;
    rdac.xy   = xy;
    rdac.ch   = ch;
    rdac.code = code;

    s_work_pkt.type     = LS_CTRL_RDAC >> 8;
    s_work_pkt.cmd      = LS_CTRL_RDAC & 0xFF;
    s_work_pkt.data_len = sizeof(ls_ctrl_rdac_t);
    memcpy(s_work_pkt.data, &rdac, sizeof(ls_ctrl_rdac_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;
    }
    return s_pack_and_send();
}

/**
 * @brief 发送设置补偿值包(0x0303)
 * @param xy    0x01=X 通道，0x02=Y 通道
 * @param value 补偿值，范围 [-2000, 2000]
 * @return 0 成功，<0 失败
 */
int ls_ctrl_set_comp(ls_radc_xy_e xy, int16_t value)
{
    memset(&s_work_pkt, 0, sizeof(s_work_pkt));

    ls_ctrl_set_comp_t comp;
    comp.xy    = xy;
    comp.value = value;

    s_work_pkt.type     = LS_CTRL_SET_COMP >> 8;
    s_work_pkt.cmd      = LS_CTRL_SET_COMP & 0xFF;
    s_work_pkt.data_len = sizeof(ls_ctrl_set_comp_t);

#if LS_ENDIAN_ENABLE
    comp.value = ls_swap_endian_16((uint16_t)comp.value);
#endif

    memcpy(s_work_pkt.data, &comp, sizeof(ls_ctrl_set_comp_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;
    }
    return s_pack_and_send();
}

/**
 * @brief 发送参数保存包(0x0304)
 * @return 0 成功，<0 失败
 */
int ls_ctrl_save_param(void)
{
    memset(&s_work_pkt, 0, sizeof(s_work_pkt));

    uint8_t save_flag = 0xFF;

    s_work_pkt.type     = LS_CTRL_SAVE_PARAM >> 8;
    s_work_pkt.cmd      = LS_CTRL_SAVE_PARAM & 0xFF;
    s_work_pkt.data_len = sizeof(uint8_t);
    memcpy(s_work_pkt.data, &save_flag, sizeof(uint8_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;
    }
    return s_pack_and_send();
}

/**************************************************************/
/*    从机处理发送函数    */
/**************************************************************/

/**
 * @brief 发送基础回复包，包含设备基础信息
 * @return int 0: 成功, -1: 失败
*/
int ls_base_reply(void)
{
    ls_base_reply_t reply;
    memset(&reply, 0, sizeof(reply));

    /*  回调从设备中获取设备信息  */
    if (s_cbs && s_cbs->get_device_info)
    {
        s_cbs->get_device_info(&reply);
    }
    else
    {
        LS_LOG_INFO("ls - get_device_info callback not registered, reply with empty info.");
    }

    /*  确认类型和命令字  */
    s_work_pkt.type = LS_BASE_REPLY >> 8;    
    s_work_pkt.cmd  = LS_BASE_REPLY & 0xFF;       //基础回复包
    /*  确认数据内容长度  */
    s_work_pkt.data_len = sizeof(ls_base_reply_t);

#if LS_ENDIAN_ENABLE     // 小端转大端
    reply.device_id      = ls_swap_endian_16(reply.device_id);
    reply.device_version = ls_swap_endian_16(reply.device_version);
    reply.comp_x         = ls_swap_endian_16(reply.comp_x);
    reply.comp_y         = ls_swap_endian_16(reply.comp_y);
#endif

    memcpy(s_work_pkt.data, &reply, sizeof(ls_base_reply_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    // 打包
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0){
        return -1;    //打包失败
    }
    return s_pack_and_send();
}

/**
 * @brief 发送控制应答包
 * @param typeCMD 控制应答命令类型
*/
int ls_ctrl_reply(uint16_t typeCMD)
{
    memset(&s_work_pkt, 0, sizeof(s_work_pkt));
    s_work_pkt.type = LS_CTRL_REPLY >> 8;    
    s_work_pkt.cmd  = LS_CTRL_REPLY & 0xFF;         //控制应答包
    s_work_pkt.data_len = sizeof(uint16_t);

#if LS_ENDIAN_ENABLE     // 小端转大端
    typeCMD = ls_swap_endian_16(typeCMD);
#endif
    memcpy(s_work_pkt.data, &typeCMD, sizeof(uint16_t));
    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    // 打包 
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;    //打包失败
    }
    return s_pack_and_send();
}


