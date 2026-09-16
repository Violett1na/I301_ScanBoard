/* stubs_ls_app_deps.c —— App 层胶水 TU 的外部依赖桩(看门狗释放用例)
 * 目的: 让宿主单测能链接 App/ls_proto_device_app.c, 直接驱动
 *   ls_app_force_watchdog_poll() 与 ls_app_poll(), 覆盖 spec §6.1 的红线
 *   ——「5s 内未收到上位机整帧即自动解除强制」。
 * 为什么全量桩: 单一 .c 的未定义符号必须全部解决才能链接(本工具链的
 *   --gc-sections 经实测不裁剪未调用的全局函数), 故按符号逐个补齐。
 *   好处是桩面完全显式: 每个桩都对应一条真实接口, 签名取自
 *   App/param.h、Bsp/ad5290.h、Port/port_*.h、Bsp/mylog.h, 接口一旦
 *   变动此处即编译期报错。代价是文件里有一批本用例不走的分支。
 * 分两节:
 *   第 1 节 —— 被测路径真正走到的桩, 带可设值与调用计数;
 *   第 2 节 —— 只为满足链接的桩(rdac/补偿/保存/设备信息/复位),
 *     一律返回"无副作用"的保守值并累加 offpath 计数; 用例结束时断言
 *     该计数为 0, 以保证断言没有偷偷依赖未建模的胶水行为。
 * 语义: 所有桩只做"记录调用 + 返回可设值", 不含判定逻辑、不触硬件。
 *   唯一带语义的是 param_force_clear: 按真实 param 层行为把强制态置回
 *   NONE(param.c 的 param_force_clear 解除强制并应用自主套), 这样
 *   "解除后看门狗再查即为未强制"这一释放态可被断言。 */
#include <stdint.h>

#include "param.h"
#include "ad5290.h"
#include "port_sys.h"
#include "port_tick.h"
#include "port_trans.h"
#include "mylog.h"

#include "stubs_ls_app_deps.h"

/* ===================== 第 1 节: 被测路径走到的桩 ===================== */

/* ---- param 层: 只桩强制态 ---- */

static uint8_t s_forced = PARAM_PROFILE_NONE;   /* 桩内强制套: 初始未强制 */
static int     s_force_clear_n = 0;             /* param_force_clear 次数 */

uint8_t param_forced(void)
{
    return s_forced;
}

int param_force_clear(void)
{
    s_force_clear_n++;
    s_forced = PARAM_PROFILE_NONE;
    return 0;
}

void stub_set_forced(uint8_t idx)
{
    s_forced = idx;
}

int stub_force_clear_count(void)
{
    return s_force_clear_n;
}

/* ---- port_tick 契约: 可设毫秒时基 ---- */

static uint32_t s_tick_ms = 0;

uint32_t port_tick_ms(void)
{
    return s_tick_ms;
}

void stub_set_tick(uint32_t ms)
{
    s_tick_ms = ms;
}

/* ---- port_trans 契约: 一次性交付字节流 ---- */

static const uint8_t *s_rx_buf = NULL;   /* 当前累积字节流(用例装入) */
static uint16_t       s_rx_len = 0;      /* 当前累积字节数 */
static int            s_rx_consume_n = 0;

uint16_t port_trans_rx_peek(const uint8_t **buf)
{
    *buf = s_rx_buf;
    return s_rx_len;
}

void port_trans_rx_consume(uint16_t len)
{
    (void)len;
    s_rx_consume_n++;
    s_rx_buf = NULL;
    s_rx_len = 0;
}

void port_trans_poll(void)
{
    /* 现实现为空操作(见 port_trans.h 契约), 桩保持一致 */
}

void stub_set_rx(const uint8_t *buf, uint16_t len)
{
    s_rx_buf = buf;
    s_rx_len = len;
}

int stub_rx_consume_count(void)
{
    return s_rx_consume_n;
}

/* ---- 日志出口: 丢弃文本, 只为满足链接 ---- */

void log_output(log_level_e level, const char *module, const char *fmt, ...)
{
    (void)level;
    (void)module;
    (void)fmt;
}

/* ============== 第 2 节: 只为满足链接的桩(本用例不走) ============== */

static int s_offpath_n = 0;   /* 第 2 节桩被调用的次数, 期望恒为 0 */

int stub_offpath_calls(void)
{
    return s_offpath_n;
}

/* 保守值: 空快照 / 零值套 / 未强制 / 写成功返回 0。不含任何副作用。 */

radc_value_t param_radc_get(void)
{
    radc_value_t r;

    s_offpath_n++;
    r.x1 = 0U;
    r.x2 = 0U;
    r.x3 = 0U;
    r.y1 = 0U;
    r.y2 = 0U;
    r.y3 = 0U;
    return r;
}

void param_radc_set(const radc_value_t *r)
{
    (void)r;
    s_offpath_n++;
}

int param_set_comp_x(int16_t v)
{
    (void)v;
    s_offpath_n++;
    return 0;
}

int param_set_comp_y(int16_t v)
{
    (void)v;
    s_offpath_n++;
    return 0;
}

int param_save(void)
{
    s_offpath_n++;
    return 0;
}

static const param_profile_t *s_offpath_profile(void)
{
    static const param_profile_t zero;   /* 全零的一套配置 */

    return &zero;
}

const param_profile_t *param_profile(uint8_t idx)
{
    (void)idx;
    s_offpath_n++;
    return s_offpath_profile();
}

int param_profile_set(uint8_t idx, const param_profile_t *p)
{
    (void)idx;
    (void)p;
    s_offpath_n++;
    return 0;
}

int param_force_set(uint8_t idx)
{
    (void)idx;
    s_offpath_n++;
    return 0;
}

uint8_t param_active(void)
{
    s_offpath_n++;
    return 0U;
}

const volatile comp_value_t *param_comp(void)
{
    static volatile comp_value_t zero;   /* 全零补偿值 */

    s_offpath_n++;
    return &zero;
}

void ad5290_set_code(ad5290_axis_e axis, ad5290_ch_e ch, uint8_t code)
{
    (void)axis;
    (void)ch;
    (void)code;
    s_offpath_n++;
}

void port_sys_reset(void)
{
    /* 真实现会复位芯片, 桩绝不模拟: 只记账 */
    s_offpath_n++;
}

int port_trans_send(const uint8_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
    s_offpath_n++;
    return 0;
}
