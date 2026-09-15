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
        case LS_BASE_REPLY_ALL:
            return handle_base_reply_all(pkt);
        case LS_CTRL_REPLY:
            return handle_ctrl_reply(pkt);
        case LS_CTRL_RDAC:
            return handle_ctrl_rdac(pkt);
        case LS_CTRL_SET_COMP:
            return handle_ctrl_set_comp(pkt);
        case LS_CTRL_SAVE_PARAM:
            return handle_ctrl_save_param(pkt);
        case LS_CTRL_SET_PROFILE:
            return handle_ctrl_set_profile(pkt);
        case LS_CTRL_GET_ALL:
            return handle_ctrl_get_all(pkt);
        case LS_CTRL_FORCE_PROFILE:
            return handle_ctrl_force_profile(pkt);
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
 * @brief 主机收到全量回复包(0x0104)：解出三套配置与工况后交回调
 * @param pkt 已解包的协议包，data 指向 36 字节全量回复载荷(大端)
 * @return 0 成功；-1 载荷长度与 ls_all_reply_t 不符(不回调)
 * @note 输出：载荷拷入栈上 ls_all_reply_t 并做大小端还原后，交
 *    s_cbs->on_reply_all；未注册回调时只记日志，不产生其它输出。
 * @note 调用关系：ls_handle() 识别 0x0104 后调用；设备侧不调用。
 * @note 副作用：只读 pkt 与 s_cbs 回调集，不触碰发送层工作缓冲；
 *    与其它接收处理函数共用 s_cbs，故不可重入。
 */
int handle_base_reply_all(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received base reply all.");

    if (pkt->data_len != sizeof(ls_all_reply_t))
    {
        LS_LOG_INFO("ls - base reply all data_len err: %u", pkt->data_len);
        return -1;
    }

    ls_all_reply_t reply;   /* 还原后的全量回复：设备信息+三套配置+工况 */
    uint8_t i;              /* 遍历三套配置的下标 */

    memcpy(&reply, pkt->data, sizeof(ls_all_reply_t));

#if LS_RX_ENDIAN_ENABLE     // 大小端互转
    reply.device_id      = ls_swap_endian_16(reply.device_id);
    reply.device_version = ls_swap_endian_16(reply.device_version);
    /*  逐套还原补偿值；radc 为单字节无需转换  */
    for (i = 0U; i < LS_PROFILE_NUM; i++)
    {
        reply.profiles[i].comp_x =
            (int16_t)ls_swap_endian_16((uint16_t)reply.profiles[i].comp_x);
        reply.profiles[i].comp_y =
            (int16_t)ls_swap_endian_16((uint16_t)reply.profiles[i].comp_y);
    }
#endif

    /*  回调处理全量回复数据  */
    if (s_cbs && s_cbs->on_reply_all)
    {
        s_cbs->on_reply_all(&reply);
    }
    else
    {
        LS_LOG_INFO("ls - on_reply_all callback not registered, ignored.");
    }
    return 0;
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

/**
 * @brief 设备收到整包写配置包(0x0305)：解析载荷并下发到应用层，
 *        随后回送控制应答包(0x0301)，data 字段填入 0x0305。
 * @param pkt 已解包的协议包，data 指向 11 字节载荷(comp 为大端)
 * @return 0 成功；-1 载荷长度不符(不应答) 或回调未注册
 * @note 输入：pkt->data 拷入栈上 ls_ctrl_set_profile_t，并按
 *    LS_RX_ENDIAN_ENABLE 把 comp_x/comp_y 还原为小端后下发。
 * @note 输出：载荷交 s_cbs->ctrl_set_profile；随后不论回调成败均经
 *    发送层 ls_ctrl_reply(0x0305) 回送控制应答帧。
 * @note 调用关系：ls_handle() 识别 0x0305 后调用；主机侧不调用。
 * @note 副作用：读 s_cbs 回调集；经发送层占用模块级工作缓冲
 *    s_work_pkt/s_work_buf/s_work_len 并触发 send 回调，故不可重入。
 */
int handle_ctrl_set_profile(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl set profile.");

    /* 数据长度校验：协议规定 11 字节(profile 1 + radc 6 + comp 4) */
    if (pkt->data_len != sizeof(ls_ctrl_set_profile_t))
    {
        LS_LOG_INFO("ls - ctrl set profile data_len err: %u", pkt->data_len);
        return -1;
    }

    ls_ctrl_set_profile_t msg;  /* 待下发载荷：套号 + 该套码值与补偿值 */
    memcpy(&msg, pkt->data, sizeof(ls_ctrl_set_profile_t));

#if LS_RX_ENDIAN_ENABLE
    msg.data.comp_x = (int16_t)ls_swap_endian_16((uint16_t)msg.data.comp_x);
    msg.data.comp_y = (int16_t)ls_swap_endian_16((uint16_t)msg.data.comp_y);
#endif

    int ret = 0;    /* 回调返回值：0 成功，非 0 由回调给出的失败码 */
    if (s_cbs && s_cbs->ctrl_set_profile)
    {
        ret = s_cbs->ctrl_set_profile(&msg);
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_set_profile callback not registered, ignored.");
        ret = -1;
    }

    /* 不论成功失败，按协议返回控制应答包 */
    (void)ls_ctrl_reply(LS_CTRL_SET_PROFILE);
    return ret;
}

/**
 * @brief 设备收到全量回读请求(0x0306)：先回控制应答，再回全量回复包。
 * @param pkt 已解包的协议包，载荷仅 1 字节(req)，内容无实义
 * @return 0 成功；-1 载荷长度不符(不应答) 或回调未注册
 * @note 输入：校验载荷长度后不使用其内容，借命令字触发一次回读。
 * @note 输出：先调 s_cbs->ctrl_get_all 通知应用层，再经发送层依次发出
 *    控制应答帧(0x0306)与全量回复帧(0x0104)。
 * @note 调用关系：ls_handle() 识别 0x0306 后调用；全量回复帧由
 *    发送层 ls_base_reply_all() 组装；主机侧不调用。
 * @note 副作用：读 s_cbs 回调集；经发送层两次占用模块级工作缓冲
 *    s_work_pkt/s_work_buf/s_work_len 并触发 send 回调，故不可重入。
 */
int handle_ctrl_get_all(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl get all.");

    /* 数据长度校验：协议规定 1 字节(req) */
    if (pkt->data_len != sizeof(ls_ctrl_get_all_t))
    {
        LS_LOG_INFO("ls - ctrl get all data_len err: %u", pkt->data_len);
        return -1;
    }

    int ret = 0;    /* 回调返回值：0 成功，非 0 由回调给出的失败码 */
    if (s_cbs && s_cbs->ctrl_get_all)
    {
        ret = s_cbs->ctrl_get_all();
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_get_all callback not registered, ignored.");
        ret = -1;
    }

    /* 按协议返回控制应答包，随后附上全量数据 */
    (void)ls_ctrl_reply(LS_CTRL_GET_ALL);
    (void)ls_base_reply_all();
    return ret;
}

/**
 * @brief 设备收到强制套包(0x0307)：解析并下发；
 *        profile == LS_PROFILE_NONE 表示解除强制。
 * @param pkt 已解包的协议包，data 指向 1 字节套号载荷
 * @return 0 成功；-1 载荷长度不符(不应答) 或回调未注册
 * @note 输入：pkt->data 拷入栈上 ls_ctrl_force_t；单字节字段无需
 *    大小端转换；0xFF 为解除强制。
 * @note 输出：载荷交 s_cbs->ctrl_force_profile；随后不论回调成败均
 *    经发送层 ls_ctrl_reply(0x0307) 回送控制应答帧。
 * @note 调用关系：ls_handle() 识别 0x0307 后调用；该命令同用作保活
 *    帧，故每次收到都必须应答；主机侧不调用。
 * @note 副作用：读 s_cbs 回调集；经发送层占用模块级工作缓冲
 *    s_work_pkt/s_work_buf/s_work_len 并触发 send 回调，故不可重入。
 */
int handle_ctrl_force_profile(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl force profile.");

    if (pkt->data_len != sizeof(ls_ctrl_force_t))
    {
        LS_LOG_INFO("ls - ctrl force profile data_len err: %u", pkt->data_len);
        return -1;
    }

    ls_ctrl_force_t msg;    /* 强制套载荷：目标套号，0xFF 为解除强制 */
    memcpy(&msg, pkt->data, sizeof(ls_ctrl_force_t));

    int ret = 0;    /* 回调返回值：0 成功，非 0 由回调给出的失败码 */
    if (s_cbs && s_cbs->ctrl_force_profile)
    {
        ret = s_cbs->ctrl_force_profile(&msg);
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_force_profile callback not registered,"
                    " ignored.");
        ret = -1;
    }

    (void)ls_ctrl_reply(LS_CTRL_FORCE_PROFILE);
    return ret;
}
