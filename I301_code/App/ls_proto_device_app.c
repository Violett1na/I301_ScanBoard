/* ls_proto_device_app.c —— LIGHTSPACE-XY 协议设备侧胶水实现
 * 2026-09-02 重构自 Proto/Src 迁入: 发送经 port_trans 契约,
 * 复位经 port_sys 契约; 接收成帧判断(协议头/长度字段)由原
 * USB 层 USBD_BULK_Recv 移入本层 ls_app_poll(协议知识归协议层,
 * 传输层只交付字节流)。 */
#include "ls_proto_device_app.h"
#include "ls_proto.h"        /* ls_device_pkt */
#include "port_trans.h"
#include "port_sys.h"
#include "ad5290.h"
#include "param.h"           /* radc/comp 经 param 接口访问(实体私有于 param.c) */
#include "mylog.h"
#include <string.h>


/* -----------------------------------------------------------------------
 * 回调实现：获取设备基础信息
 * ----------------------------------------------------------------------- */

/* 发送数据回调，底层经 port_trans(USB BULK)发送 */
static void app_send_data(uint8_t *buf, uint16_t len)
{
    /* 忙即放弃本次, 与历史行为一致 */
    (void)port_trans_send(buf, len);
}

static void app_reset_device(void)
{
    LOG_LSNET_INFO("ls - reset device.");
    port_sys_reset();
}

/* 控制数字电位器回调：把协议字段映射到 AD5290 驱动接口
 * 协议侧：xy 0x01/0x02 → X/Y；ch 0x01/0x02/0x03 → 通道 1/2/3
 * 驱动侧：ad5290_axis_e (0/1)，ad5290_ch_e (0/1/2)
 * 返回: 0 成功，-1 参数非法 */
static int app_ctrl_rdac(const ls_ctrl_rdac_t *rdac)
{
    radc_value_t r;

    if (rdac == NULL)
    {
        return -1;
    }

    r = param_radc_get();

    if (rdac->xy == LS_RADC_X)
    {
        switch (rdac->ch)
        {
            case LS_RADC_CH_1:
                r.x1 = rdac->code;
                ad5290_set_code(AD5290_AXIS_X, AD5290_CH_1, rdac->code);
                break;
            case LS_RADC_CH_2:
                r.x2 = rdac->code;
                ad5290_set_code(AD5290_AXIS_X, AD5290_CH_2, rdac->code);
                break;
            case LS_RADC_CH_3:
                r.x3 = rdac->code;
                ad5290_set_code(AD5290_AXIS_X, AD5290_CH_3, rdac->code);
                break;
            default:
                return -1;
        }
    }
    else if (rdac->xy == LS_RADC_Y)
    {
        switch (rdac->ch)
        {
            case LS_RADC_CH_1:
                r.y1 = rdac->code;
                ad5290_set_code(AD5290_AXIS_Y, AD5290_CH_1, rdac->code);
                break;
            case LS_RADC_CH_2:
                r.y2 = rdac->code;
                ad5290_set_code(AD5290_AXIS_Y, AD5290_CH_2, rdac->code);
                break;
            case LS_RADC_CH_3:
                r.y3 = rdac->code;
                ad5290_set_code(AD5290_AXIS_Y, AD5290_CH_3, rdac->code);
                break;
            default:
                return -1;
        }
    }
    else
    {
        LOG_LSNET_INFO("ls - rdac xy invalid: 0x%02X", rdac->xy);
        return -1;
    }

    param_radc_set(&r);
    LOG_LSNET_INFO("ls - rdac %03d  %03d  %03d  %03d  %03d  %03d",
                   r.x1, r.x2, r.x3,
                   r.y1, r.y2, r.y3);
    return 0;
}

/* 设置补偿值回调：将协议下发的补偿值应用到对应通道
 * 协议侧：xy 0x01/0x02 → X/Y；value 范围 [-2000, 2000]
 * 返回: 0 成功，-1 参数非法 */
static int app_ctrl_set_comp(const ls_ctrl_set_comp_t *comp)
{
    if (comp == NULL)
    {
        return -1;
    }

    /* 范围校验收敛于 param setter(协议契约 [-2000, 2000]) */
    if (comp->xy == LS_RADC_X)
    {
        if (param_set_comp_x(comp->value) != 0)
        {
            return -1;
        }
        LOG_LSNET_INFO("ls - set comp X: %d", comp->value);
    }
    else if (comp->xy == LS_RADC_Y)
    {
        if (param_set_comp_y(comp->value) != 0)
        {
            return -1;
        }
        LOG_LSNET_INFO("ls - set comp Y: %d", comp->value);
    }
    else
    {
        LOG_LSNET_INFO("ls - set comp xy invalid: 0x%02X", comp->xy);
        return -1;
    }

    return 0;
}

/* 参数保存回调：将当前参数写入持久化存储
 * 返回: 0 成功，-1 失败 */
static int app_ctrl_save_param(void)
{
    LOG_LSNET_INFO("ls - save param.");
    return param_save();
}

static void app_get_device_info(ls_base_reply_t *reply)
{
    radc_value_t r;
    const volatile comp_value_t *pc;

    if (reply == NULL)
    {
        return;
    }
    reply->device_id      = 0x1234;
    reply->device_version = 0x5678;

    r  = param_radc_get();
    pc = param_comp();

    reply->r_x1           = r.x1;
    reply->r_x2           = r.x2;
    reply->r_x3           = r.x3;
    reply->r_y1           = r.y1;
    reply->r_y2           = r.y2;
    reply->r_y3           = r.y3;
    reply->comp_x         = pc->x;
    reply->comp_y         = pc->y;
}

/* 初始化协议应用层，注册所有回调 */
void ls_app_init(void)
{
    static const ls_receive_callbacks_t r_cbs = {
        .reset_device    = app_reset_device,
        .ctrl_rdac       = app_ctrl_rdac,
        .ctrl_set_comp   = app_ctrl_set_comp,
        .ctrl_save_param = app_ctrl_save_param,
    };

    static const ls_trans_callbacks_t t_cbs = {
        .send            = app_send_data,
        .get_device_info = app_get_device_info,
    };

    ls_receiver_init_callbacks(&r_cbs);
    ls_trans_init_callbacks(&t_cbs);
    LOG_LSNET_INFO("ls - callbacks initialized.");
}

/* 主循环接收处理: 传输层字节流 → 成帧判断 → 协议分发。
 * 成帧语义承自原 USB 层 USBD_BULK_Recv(逐字节迁移, 逻辑不变):
 * 头部字符串不匹配 = 坏帧整体丢弃; 累积长度 == 包内长度字段 = 整帧就绪。 */
void ls_app_poll(void)
{
    const uint8_t *buf = NULL;
    uint16_t len = port_trans_rx_peek(&buf);
    uint16_t flen;

    port_trans_poll();

    if ((buf == NULL) || (len < LS_DATA_BASE_LEN))
    {
        return;
    }

    if (memcmp(buf, LS_HEADER_STR, LS_HEADER_LEN) != 0)
    {
        LOG_SYS_ERROR("recv head error.");
        port_trans_rx_consume(len);
        return;
    }

    flen = (uint16_t)(((uint16_t)buf[13] << 8) | buf[14]);
    if (len == flen)
    {
        ls_parse(&ls_device_pkt, (uint8_t *)buf, flen);
        port_trans_rx_consume(flen);
    }
}
