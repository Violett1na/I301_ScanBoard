#include "ls_proto_device_app.h"
#include "usbd_bulk.h"


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
 * @brief 获取设备基础信息回调实现，填充设备版本等信息
 */
static void app_get_device_info(ls_base_reply_t *reply)
{

    
}

static void app_on_pid(uint8_t sel, int16_t value)
{

}


/**
 * @brief 初始化 ls proto 应用层，注册所有回调
 *
 */
void ls_app_init(void)
{
    static const ls_receive_callbacks_t r_cbs = {
        .reset_device    = app_reset_device,
    };

    static const ls_trans_callbacks_t t_cbs = {
        .send            = app_send_data,
        .get_device_info = app_get_device_info,
    };

    ls_receiver_init_callbacks(&r_cbs);
    ls_trans_init_callbacks(&t_cbs);
    LOG_LSNET_INFO("ls - callbacks initialized.");
}