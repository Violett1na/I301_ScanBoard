#include "ls_proto_receive.h"
#include "ls_proto_trans.h"

static const ls_receive_callbacks_t *s_cbs = NULL;  /* 已注册的回调集合 */

/*  注册回调函数集合  */
void ls_receiver_init_callbacks(const ls_receive_callbacks_t *cbs)
{
    s_cbs = cbs;
}

/* -----------------------------------------------------------------------
 * 内部处理函数
 * ----------------------------------------------------------------------- */

/*  解析协议包内容  */
int ls_parse(ls_packet_t *pkt, uint8_t *in_buf, uint16_t in_len)
{
    // LS_LOG_INFO("ls - parsing data, len=%u", in_len);
    /*  按字节解包  */
    if (ls_unpack(in_buf, in_len, pkt) < 0)
    {
        LS_LOG_INFO("ls - failed to parse data.");
        return -1;
    }

    return ls_handle(pkt);
}


/*   按命令字执行句柄   */
int ls_handle(ls_packet_t *pkt)
{
    uint16_t typeCMD = ((uint16_t)pkt->type << 8) | (uint16_t)pkt->cmd;

    switch (typeCMD)
    {
        case LS_BASE_QUERY:
            return handle_base_query();
        case LS_BASE_RESET_DEVICE:
            return handle_base_reset_device();
        case LS_BASE_REPLY:
            return handle_base_reply(pkt);
        case LS_CTRL_REPLY:
            return handle_ctrl_reply(pkt);
        case LS_CTRL_RDAC:
            return handle_ctrl_rdac(pkt);
        case LS_CTRL_SET_COMP:
            return handle_ctrl_set_comp(pkt);
        case LS_CTRL_SAVE_PARAM:
            return handle_ctrl_save_param(pkt);
        default:
            LS_LOG_INFO("ls - unknown typeCMD: 0x%04X", typeCMD);
            return -1;
    }
}

/******************************************************************/
/*    主机处理接收函数    */
/*****************************************************************/

/**
 * @brief 主机收到基础回复包，处理基础回复包：调用回调函数处理回复数据
*/
int handle_base_reply(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received base reply.");
    ls_base_reply_t reply;
    memcpy(&reply, pkt->data, sizeof(ls_base_reply_t));
#if LS_RX_ENDIAN_ENABLE     // 大小端互转一次
    reply.device_id      = ls_swap_endian_16(reply.device_id);
    reply.device_version = ls_swap_endian_16(reply.device_version);
    reply.comp_x         = ls_swap_endian_16(reply.comp_x);
    reply.comp_y         = ls_swap_endian_16(reply.comp_y);
#endif
    /*  回调处理回复数据  */
    if (s_cbs && s_cbs->on_reply)
    {
        s_cbs->on_reply(&reply);
    }
    else
    {
        LS_LOG_INFO("ls - on_reply callback not registered, reply ignored.");
    }
    return -1; 
}

/**
 * @brief 主机收到控制应答包，处理控制应答包：调用回调函数处理应答包
*/
int handle_ctrl_reply(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl reply.");
    uint16_t typeCMD = pkt->data[0] << 8 | pkt->data[1];
    if (s_cbs && s_cbs->on_ctrl_reply)
    {
        s_cbs->on_ctrl_reply(typeCMD);
    }
    else
    {
        LS_LOG_INFO("ls - on_ctrl_reply callback not registered, reply ignored.");
    }
    return -1; 
}


/******************************************************************/
/*    从机处理接收函数    */
/*****************************************************************/
/**
 * @brief 设备收到查询包，处理查询包：构造并输出基础信息应答包
 */
int handle_base_query(void)
{
    LS_LOG_INFO("ls - received base query.");

    /*  收到查询包之后，构建应答包进行回复  */
    return ls_base_reply();
}

/**
 * @brief 设备收到重启包，处理重启包：调用回调函数重启设备
*/
int handle_base_reset_device(void)
{
    LS_LOG_INFO("ls - received base reset device.");
    //  调用设备重启函数  */
    if (s_cbs && s_cbs->reset_device)
    {
        s_cbs->reset_device();
    }
    else
    {
        LS_LOG_INFO("ls - reset_device callback not registered, reset ignored.");
    }
    return -1;
}

/**
 * @brief 设备收到数字电位器控制包(0x0302)：解析载荷并下发到应用层，
 *        随后回送控制应答包(0x0301)，data 字段填入 0x0302。
 * @return 0 成功，<0 失败
 */
int handle_ctrl_rdac(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl rdac.");

    /* 数据长度校验：协议规定 3 字节（xy / ch / code），全部为单字节 */
    if (pkt->data_len != sizeof(ls_ctrl_rdac_t))
    {
        LS_LOG_INFO("ls - ctrl rdac data_len err: %u", pkt->data_len);
        return -1;
    }

    ls_ctrl_rdac_t rdac;
    memcpy(&rdac, pkt->data, sizeof(ls_ctrl_rdac_t));
    /* 全部为单字节字段，无需做大小端转换 */

    int ret = 0;
    if (s_cbs && s_cbs->ctrl_rdac)
    {
        ret = s_cbs->ctrl_rdac(&rdac);
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_rdac callback not registered, ignored.");
        ret = -1;
    }

    /* 不论成功失败，按协议返回控制应答包，便于上位机做超时与重传管理 */
    (void)ls_ctrl_reply(LS_CTRL_RDAC);
    return ret;
}

/**
 * @brief 设备收到设置补偿值包(0x0303)：解析载荷并下发到应用层，
 *        随后回送控制应答包(0x0301)，data 字段填入 0x0303。
 * @return 0 成功，<0 失败
 */
int handle_ctrl_set_comp(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl set comp.");

    /* 数据长度校验：协议规定 3 字节（xy 1字节 + value 2字节） */
    if (pkt->data_len != sizeof(ls_ctrl_set_comp_t))
    {
        LS_LOG_INFO("ls - ctrl set comp data_len err: %u", pkt->data_len);
        return -1;
    }

    ls_ctrl_set_comp_t comp;
    memcpy(&comp, pkt->data, sizeof(ls_ctrl_set_comp_t));

#if LS_RX_ENDIAN_ENABLE
    comp.value = (int16_t)ls_swap_endian_16((uint16_t)comp.value);
#endif

    int ret = 0;
    if (s_cbs && s_cbs->ctrl_set_comp)
    {
        ret = s_cbs->ctrl_set_comp(&comp);
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_set_comp callback not registered, ignored.");
        ret = -1;
    }

    /* 按协议返回控制应答包 */
    (void)ls_ctrl_reply(LS_CTRL_SET_COMP);
    return ret;
}

/**
 * @brief 设备收到参数保存包(0x0304)：调用回调执行保存，
 *        随后回送控制应答包(0x0301)，data 字段填入 0x0304。
 * @return 0 成功，<0 失败
 */
int handle_ctrl_save_param(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl save param.");
    (void)pkt;

    int ret = 0;
    if (s_cbs && s_cbs->ctrl_save_param)
    {
        ret = s_cbs->ctrl_save_param();
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_save_param callback not registered, ignored.");
        ret = -1;
    }

    /* 按协议返回控制应答包 */
    (void)ls_ctrl_reply(LS_CTRL_SAVE_PARAM);
    return ret;
}
