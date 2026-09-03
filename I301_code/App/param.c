/* param.c —— 参数层实现: 参数实体私有 + 接口收敛 + 初始化包装
 * 封装: s_radc/s_comp 为模块私有(static), 写方收敛于本文件
 *   (param_init 加载 / setter 协议写), 范围校验收敛于 comp_set_checked
 *   与 setter; ISR 经 param_comp() 只读指针直读。
 * 上下游: main.c 按启动序列调用; 协议层经 param.h 接口读写;
 *   ISR 只读 comp(见 ad_da_alg.c 线性算法)。
 * 2026-09-02 重构: 去 HAL(延时经 port_tick); bsp_flash 改不透明
 *   blob 接口; 协议栈初始化包装(lsnet_init)移回 main(归传输装配)。 */
#include "param.h"
#include "ad5290.h"
#include "bsp_flash.h"
#include "port_tick.h"
#include "mylog.h"

/* 编译期尺寸约束: 持久化结构不得超出存储层数据区(规范 10-10) */
typedef char param_flash_fit_check[
    (sizeof(flash_store_t) <= BSP_FLASH_DATA_MAX) ? 1 : -1];

static radc_value_t s_radc;          /* 电位器码值: param_init/协议写(主循环域) */
static volatile comp_value_t s_comp; /* 偏置补偿: 主循环写、ISR 读; 对齐 16 位加载天然原子, 不加锁 */

/* AD5290 电位器初始化包装(main.c 启动序列调用) */
void ad5290_set_init(void)
{
    ad5290_init();
    port_delay_ms(10);
}

/* 默认电位器码值(承自原仓库, 电气依据待补) */
#define PARAM_DEF_X1   40
#define PARAM_DEF_X2   80
#define PARAM_DEF_X3   45
#define PARAM_DEF_Y1   40
#define PARAM_DEF_Y2   80
#define PARAM_DEF_Y3   45
#define PARAM_DEF_COMP (-80)   /* 偏置补偿默认值(承自原仓库) */

/* ---- 读写接口 ---- */

const volatile comp_value_t *param_comp(void)
{
    return &s_comp;
}

int param_set_comp_x(int16_t v)
{
    if ((v < COMP_VALUE_MIN) || (v > COMP_VALUE_MAX))
    {
        LOG_SYS_ERROR("comp x out of range: %d", v);
        return -1;
    }
    s_comp.x = v;
    return 0;
}

int param_set_comp_y(int16_t v)
{
    if ((v < COMP_VALUE_MIN) || (v > COMP_VALUE_MAX))
    {
        LOG_SYS_ERROR("comp y out of range: %d", v);
        return -1;
    }
    s_comp.y = v;
    return 0;
}

radc_value_t param_radc_get(void)
{
    return s_radc;
}

void param_radc_set(const radc_value_t *r)
{
    if (r == NULL)
    {
        return;
    }
    s_radc = *r;
}

int param_save(void)
{
    flash_store_t store;

    store.radc = s_radc;
    store.comp = s_comp;
    return bsp_flash_save(&store, sizeof(store));
}

/* comp 一致性写入: 任一越界整体回退默认, 防非法偏置进入 ISR(规范 10-2) */
static void comp_set_checked(int16_t x, int16_t y)
{
    if ((x < COMP_VALUE_MIN) || (x > COMP_VALUE_MAX) ||
        (y < COMP_VALUE_MIN) || (y > COMP_VALUE_MAX))
    {
        s_comp.x = PARAM_DEF_COMP;
        s_comp.y = PARAM_DEF_COMP;
        LOG_SYS_ERROR("comp out of range, fallback default");
    }
    else
    {
        s_comp.x = x;
        s_comp.y = y;
    }
}

/* 参数初始化: flash 加载(magic+CRC 已由 bsp_flash 校验), 失败回退默认;
 * 加载后 comp 做一致性范围检查, 越界回退默认; 最后写电位器。 */
void param_init(void)
{
    flash_store_t store;

    if (bsp_flash_load(&store, sizeof(store)) == 0)
    {
        s_radc = store.radc;
        comp_set_checked(store.comp.x, store.comp.y);
        LOG_SYS_INFO("load param from flash");
    }
    else
    {
        s_radc.x1 = PARAM_DEF_X1;
        s_radc.x2 = PARAM_DEF_X2;
        s_radc.x3 = PARAM_DEF_X3;
        s_radc.y1 = PARAM_DEF_Y1;
        s_radc.y2 = PARAM_DEF_Y2;
        s_radc.y3 = PARAM_DEF_Y3;

        s_comp.x  = PARAM_DEF_COMP;
        s_comp.y  = PARAM_DEF_COMP;
        LOG_SYS_INFO("load param from default");
    }

    ad5290_set_all_code((const uint8_t *)&s_radc);
    LOG_SYS_INFO("param: x1 = %04d, x2 = %04d, x3 = %04d, y1 = %04d, y2 = %04d, y3 = %04d",
                 s_radc.x1, s_radc.x2, s_radc.x3, s_radc.y1, s_radc.y2, s_radc.y3);
    LOG_SYS_INFO("comp: x = %04d, y = %04d", s_comp.x, s_comp.y);
    LOG_SYS_INFO("===================================================");
}

/* file end */
