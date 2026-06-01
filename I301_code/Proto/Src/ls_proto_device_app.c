#include "ls_proto_device_app.h"
#include "usbd_bulk.h"
#include "ad5290.h"


/* -----------------------------------------------------------------------
 * 回调实现：获取设备基础信息
 * ----------------------------------------------------------------------- */

/**
 * @brief 发送数据回调，底层通过USB BULK发送数据
 */
static void app_send_data(uint8_t *buf, uint16_t len)
{
    USBD_BULK_SendLarge(buf, len);
}

static void app_reset_device(void)
{
    LOG_LSNET_INFO("ls - reset device.");
    __NVIC_SystemReset();

}

/**
 * @brief 控制数字电位器回调：把协议字段映射到 AD5290 驱动接口
 *        协议侧：xy 0x01/0x02 → X/Y；ch 0x01/0x02/0x03 → 通道 1/2/3
 *        驱动侧：ad5290_axis_e (0/1)，ad5290_ch_e (0/1/2)
 * @return 0 成功，-1 参数非法
 */
static int app_ctrl_rdac(const ls_ctrl_rdac_t *rdac)
{
    if (rdac == NULL)
    {
        return -1;
    }
    ad5290_axis_e axis;
    switch (rdac->xy)
    {
        case LS_RADC_X: axis = AD5290_AXIS_X; break;
        case LS_RADC_Y: axis = AD5290_AXIS_Y; break;
        default:
            LOG_LSNET_INFO("ls - rdac xy invalid: 0x%02X", rdac->xy);
            return -1;
    }

    ad5290_ch_e ch;
    switch (rdac->ch)   
    {
        case LS_RADC_CH_1: ch = AD5290_CH_1; break;
        case LS_RADC_CH_2: ch = AD5290_CH_2; break;
        case LS_RADC_CH_3: ch = AD5290_CH_3; break;
        default:
            LOG_LSNET_INFO("ls - rdac ch invalid: 0x%02X", rdac->ch);
            return -1;
    }

    AD5290_SetCode(axis, ch, rdac->code);
    LOG_LSNET_INFO("ls - rdac set xy=0x%02X ch=0x%02X code=%u",
                   rdac->xy, rdac->ch, rdac->code);
    return 0;
}

static void app_get_device_info(ls_base_reply_t *reply)
{
    if (reply == NULL)
    {
        return;
    }
    reply->device_id      = 0x1234;
    reply->device_version = 0x5678;
}

/**
 * @brief 初始化 ls proto 应用层，注册所有回调
 *
 */
void ls_app_init(void)
{
    static const ls_receive_callbacks_t r_cbs = {
        .reset_device    = app_reset_device,
        .ctrl_rdac       = app_ctrl_rdac,
    };

    static const ls_trans_callbacks_t t_cbs = {
        .send            = app_send_data,
        .get_device_info = app_get_device_info,
    };

    ls_receiver_init_callbacks(&r_cbs);
    ls_trans_init_callbacks(&t_cbs);
    LOG_LSNET_INFO("ls - callbacks initialized.");
}