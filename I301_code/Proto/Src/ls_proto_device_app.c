#include "ls_proto_device_app.h"
#include "usbd_bulk.h"
#include "ad5290.h"
#include "task.h"


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
    if (rdac->xy == LS_RADC_X)
    {
        switch (rdac->ch)   
        {
            case LS_RADC_CH_1: 
                radc_value.x1 = rdac->code; 
                AD5290_SetCode(AD5290_AXIS_X, AD5290_CH_1, rdac->code);
                break;
            case LS_RADC_CH_2: 
                radc_value.x2 = rdac->code; 
                AD5290_SetCode(AD5290_AXIS_X, AD5290_CH_2, rdac->code);
                break;
            case LS_RADC_CH_3: 
                radc_value.x3 = rdac->code; 
                AD5290_SetCode(AD5290_AXIS_X, AD5290_CH_3, rdac->code);
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
                radc_value.y1 = rdac->code; 
                AD5290_SetCode(AD5290_AXIS_Y, AD5290_CH_1, rdac->code);
                break;
            case LS_RADC_CH_2: 
                radc_value.y2 = rdac->code; 
                AD5290_SetCode(AD5290_AXIS_Y, AD5290_CH_2, rdac->code);
                break;
            case LS_RADC_CH_3: 
                radc_value.y3 = rdac->code; 
                AD5290_SetCode(AD5290_AXIS_Y, AD5290_CH_3, rdac->code);
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

    LOG_LSNET_INFO("ls - rdac %03d  %03d  %03d  %03d  %03d  %03d",
                   radc_value.x1, radc_value.x2, radc_value.x3,
                   radc_value.y1, radc_value.y2, radc_value.y3);
    return 0;
}

/**
 * @brief 设置补偿值回调：将协议下发的补偿值应用到对应通道
 *        协议侧：xy 0x01/0x02 → X/Y；value 范围 [-2000, 2000]
 * @return 0 成功，-1 参数非法
 */
static int app_ctrl_set_comp(const ls_ctrl_set_comp_t *comp)
{
    if (comp == NULL)
    {
        return -1;
    }

    if (comp->xy == LS_RADC_X)
    {
        /* TODO: 应用 X 通道补偿值 comp->value */
        comp_value.x = comp->value;
        LOG_LSNET_INFO("ls - set comp X: %d", comp->value);
    }
    else if (comp->xy == LS_RADC_Y)
    {
        /* TODO: 应用 Y 通道补偿值 comp->value */
        comp_value.y = comp->value;
        LOG_LSNET_INFO("ls - set comp Y: %d", comp->value);
    }
    else
    {
        LOG_LSNET_INFO("ls - set comp xy invalid: 0x%02X", comp->xy);
        return -1;
    }

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

    reply->r_x1           = radc_value.x1;
    reply->r_x2           = radc_value.x2;
    reply->r_x3           = radc_value.x3;
    reply->r_y1           = radc_value.y1;
    reply->r_y2           = radc_value.y2;
    reply->r_y3           = radc_value.y3;
    reply->comp_x         = comp_value.x;
    reply->comp_y         = comp_value.y;
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
        .ctrl_set_comp   = app_ctrl_set_comp,
    };

    static const ls_trans_callbacks_t t_cbs = {
        .send            = app_send_data,
        .get_device_info = app_get_device_info,
    };

    ls_receiver_init_callbacks(&r_cbs);
    ls_trans_init_callbacks(&t_cbs);
    LOG_LSNET_INFO("ls - callbacks initialized.");
}