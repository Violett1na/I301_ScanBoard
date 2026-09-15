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
#include <stddef.h>          /* offsetof: 布局静态断言用 */


/* 编译期布局约束: 协议层 ls_profile_t 与 App 层 param_profile_t 是两个
 * 独立定义(分层不得直接共用类型, 规范 13-3), 仅靠"字节布局一致"的约定
 * 对齐 —— 0x0305 下行/0x0104 上行两侧字节流兼容全系于此。任一侧字段
 * 顺序或宽度改动而未同步另一侧, 报文会静默错位, 故以 C99 兼容的负尺寸
 * typedef char 手法(同 param.c 的 param_flash_fit_check)在编译期钉死
 * 尺寸与 comp 成员偏移: 尺寸相等(10B) + comp_x/comp_y 偏移与 param 侧
 * comp/comp.y 逐一相等。 */
typedef char ls_profile_layout_check[
    ((sizeof(ls_profile_t) == sizeof(param_profile_t)) &&
     (offsetof(ls_profile_t, comp_x) ==
      offsetof(param_profile_t, comp)) &&
     (offsetof(ls_profile_t, comp_y) ==
      (offsetof(param_profile_t, comp) + offsetof(comp_value_t, y))))
    ? 1 : -1];


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

/* app_ctrl_set_profile —— 整包写一套配置回调(命令字 0x0305)
 * 目的: 把协议载荷的 10B 单套配置(6 路码值 + X/Y 补偿)整包写入参数层
 *   第 profile 套, 供上位机下发与联调。
 * 输入: msg —— 协议整包写载荷(目标套号 + 10B 配置); NULL 拒收。协议侧
 *   radc[6] 顺序 = X1 X2 X3 Y1 Y2 Y3, 与 param_profile_t 一致; comp_x/
 *   comp_y 已由接收层按 LS_RX_ENDIAN_ENABLE 还原为小端。
 * 输出: 无(结果落在参数层存储)。
 * 返回值: 0 成功; -1 msg 为空或参数层拒收(套号越界)。
 * 调用关系: 协议层 handle_ctrl_set_profile 经 ctrl_set_profile 回调调用;
 *   内部调 param_profile_set。
 * 副作用: 经 param 层写内存态; 若写的正是生效套, param 层会一并写
 *   AD5290 硬件码值(间接硬件副作用); 不落 flash; 输出一条 INFO 日志。 */
static int app_ctrl_set_profile(const ls_ctrl_set_profile_t *msg)
{
    param_profile_t p;   /* 待写入的一套配置(映射自协议字段) */

    if (msg == NULL)
    {
        return -1;
    }

    /* 协议侧 radc[6] 顺序 = X1 X2 X3 Y1 Y2 Y3, 与 param_profile_t 一致 */
    p.radc.x1 = msg->data.radc[0];
    p.radc.x2 = msg->data.radc[1];
    p.radc.x3 = msg->data.radc[2];
    p.radc.y1 = msg->data.radc[3];
    p.radc.y2 = msg->data.radc[4];
    p.radc.y3 = msg->data.radc[5];
    p.comp.x  = msg->data.comp_x;
    p.comp.y  = msg->data.comp_y;

    /* 索引越界与 comp 范围校验均收敛于 param 层 */
    if (param_profile_set(msg->profile, &p) != 0)
    {
        return -1;
    }

    LOG_SYS_INFO("ls - set profile %u: %03u %03u %03u %03u %03u %03u "
                 "comp %d %d",
                 msg->profile,
                 p.radc.x1, p.radc.x2, p.radc.x3,
                 p.radc.y1, p.radc.y2, p.radc.y3,
                 p.comp.x, p.comp.y);
    return 0;
}

/* app_ctrl_get_all —— 全量回读请求回调(命令字 0x0306)
 * 目的: 通知设备侧应用层收到一次全量回读请求。
 * 输入: 无。
 * 输出: 无。
 * 返回值: 恒 0(成功)。数据由发送层经 get_device_info_all 回调另取。
 * 调用关系: 协议层 handle_ctrl_get_all 经 ctrl_get_all 回调调用; 本回调
 *   无额外副作用, 实为占位。
 * 副作用: 仅输出一条 INFO 日志(不碰硬件, 不改全局态)。 */
static int app_ctrl_get_all(void)
{
    LOG_SYS_INFO("ls - get all.");
    return 0;
}

/* app_ctrl_force_profile —— 强制套回调(命令字 0x0307, 兼作保活帧)
 * 目的: 强制生效到指定套, 或解除强制交还自主选套。
 * 输入: msg —— 强制套载荷; profile 0~2 为目标套, LS_PROFILE_NONE(0xFF)
 *   表示解除强制; NULL 拒收。
 * 输出: 无(结果落在参数层存储与硬件)。
 * 返回值: 0 成功; -1 msg 为空或参数层拒收(套号越界)。
 * 调用关系: 协议层 handle_ctrl_force_profile 经 ctrl_force_profile 回调
 *   调用; 内部按需调 param_force_clear / param_force_set。
 * 副作用: 经 param 层写易失强制态并写 AD5290 硬件码值(间接硬件副作用);
 *   强制态不落 flash; 输出一条 INFO 日志。 */
static int app_ctrl_force_profile(const ls_ctrl_force_t *msg)
{
    if (msg == NULL)
    {
        return -1;
    }

    if (msg->profile == LS_PROFILE_NONE)
    {
        (void)param_force_clear();
        LOG_SYS_INFO("ls - force released, back to auto");
        return 0;
    }

    if (param_force_set(msg->profile) != 0)
    {
        return -1;
    }
    LOG_SYS_INFO("ls - force profile %u", msg->profile);
    return 0;
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

/* app_get_device_info_all —— 全量回复数据源回调(命令字 0x0104)
 * 目的: 为发送层组装的全量回复包填充设备信息、三套配置与当前工况。
 * 输入: reply —— 发送层栈上传入的待填充回复结构(进入前已 memset 清零);
 *   NULL 直接返回。本回调只读参数层, 不读协议载荷。
 * 输出: 写满 reply 的 device_id/device_version/active_profile/
 *   forced_profile 与 profiles[3](各 10B); 写的是调用方对象。
 * 返回值: 无。
 * 调用关系: 发送层 ls_base_reply_all 经 get_device_info_all 回调调用
 *   (由 0x0306 全量回读请求触发); 数据经 param_profile/param_active/
 *   param_forced 取自参数层。
 * 副作用: 无硬件写入、不改参数层与全局态; 仅取 param_profile 的只读
 *   活视图(取值即用, 不跨写操作持有)。 */
static void app_get_device_info_all(ls_all_reply_t *reply)
{
    const param_profile_t *p;   /* 第 i 套配置的只读视图(参数层内部存储) */
    uint8_t i;                  /* 套索引: 0 ~ LS_PROFILE_NUM-1 */

    if (reply == NULL)
    {
        return;
    }

    reply->device_id      = 0x1234;
    reply->device_version = 0x5678;
    reply->active_profile = param_active();
    reply->forced_profile = param_forced();

    for (i = 0U; i < LS_PROFILE_NUM; i++)
    {
        p = param_profile(i);
        if (p == NULL)
        {
            continue;
        }
        reply->profiles[i].radc[0] = p->radc.x1;
        reply->profiles[i].radc[1] = p->radc.x2;
        reply->profiles[i].radc[2] = p->radc.x3;
        reply->profiles[i].radc[3] = p->radc.y1;
        reply->profiles[i].radc[4] = p->radc.y2;
        reply->profiles[i].radc[5] = p->radc.y3;
        reply->profiles[i].comp_x  = p->comp.x;
        reply->profiles[i].comp_y  = p->comp.y;
    }
}

/* 初始化协议应用层，注册所有回调 */
void ls_app_init(void)
{
    static const ls_receive_callbacks_t r_cbs = {
        .reset_device       = app_reset_device,
        .ctrl_rdac          = app_ctrl_rdac,
        .ctrl_set_comp      = app_ctrl_set_comp,
        .ctrl_save_param    = app_ctrl_save_param,
        .ctrl_set_profile   = app_ctrl_set_profile,
        .ctrl_get_all       = app_ctrl_get_all,
        .ctrl_force_profile = app_ctrl_force_profile,
    };

    static const ls_trans_callbacks_t t_cbs = {
        .send                = app_send_data,
        .get_device_info     = app_get_device_info,
        .get_device_info_all = app_get_device_info_all,
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
