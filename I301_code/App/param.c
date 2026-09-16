/* param.c —— 参数层实现: 三套配置存储私有 + 生效套应用 + 接口收敛
 * 模型: s_store 恒存三套(0=30k 大角度 1=40k 小角度 2=过流降速); 生效套
 *   = 强制优先于自主: s_forced != PARAM_PROFILE_NONE 时取强制套, 否则取
 *   s_active。既有读写接口一律作用于生效套。
 * 封装: s_store/s_active/s_forced/s_eff/s_comp 为模块私有(static), 写方
 *   收敛于本文件(param_init 加载 / 套切换 / setter 协议写); 整包写与加载
 *   的 comp 范围校验收敛于 comp_checked, 单点 setter 越界拒收仍在原位;
 *   ISR 经 param_comp() 只读指针直读。
 * 上下游: main.c 按启动序列调用; 协议层经 param.h 接口读写;
 *   ISR 只读 comp(见 ad_da_alg.c 线性算法)。
 * 2026-09-02 重构: 去 HAL(延时经 port_tick); bsp_flash 改不透明
 *   blob 接口; 协议栈初始化包装(lsnet_init)移回 main(归传输装配)。
 * 2026-09-15 三套配置: 单套模型扩为三套 + 生效/强制索引(spec §4.2)。 */
#include "param.h"
#include "ad5290.h"
#include "bsp_flash.h"
#include "port_tick.h"
#include "mylog.h"

/* 编译期尺寸约束: 持久化结构不得超出存储层数据区(规范 10-10) */
typedef char param_flash_fit_check[
    (sizeof(flash_store_t) <= BSP_FLASH_DATA_MAX) ? 1 : -1];

/* 三套配置存储(flash 镜像): 本文件写、本文件读(主循环域) */
static flash_store_t s_store;

/* 自主生效套: param_active_set 写、本文件读(主循环域) */
static volatile uint8_t s_active;

/* 强制套: param_force_set/param_force_clear 写、本文件读(主循环域);
 * PARAM_PROFILE_NONE = 未强制; 易失态, 不落 flash */
static uint8_t s_forced;

/* 实际生效套 = 强制优先于自主: 本文件写、本文件读(主循环域) */
static uint8_t s_eff;

/* 生效套 comp 镜像: 主循环写、ISR 读; 对齐 16 位加载天然原子, 不加锁 */
static volatile comp_value_t s_comp;

/* ad5290_set_init —— AD5290 电位器初始化包装
 * 目的: 完成电位器初始电平(CS 高、全 SCL 低), 供启动序列调用。
 * 输入: 无。
 * 输出: 无(仅硬件电平)。
 * 返回值: 无。
 * 调用关系: main.c 启动序列调用; 内部调 ad5290_init + port_delay_ms。
 * 副作用: 触碰 AD5290 片上 CS/SCL 电平(经 ad5290 位带 SPI); 忙等 10ms。 */
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

/* comp_checked —— 单套 comp 范围一致性检查与整体回退
 * 目的: 把"comp 越界即整体双字段回退默认并告警"的策略收敛到单点, 供
 *   整包写与上电加载两条路径共用(规范 10-2)。
 * 输入: c —— 指向待检查的一套配置的 comp 字段; 调用方保证非 NULL。
 * 输出: 越界时整体改写 c->x 与 c->y 为 PARAM_DEF_COMP; 在域内则不动。
 * 返回值: 无。
 * 调用关系: param_profile_set / param_init 内部调用(本文件私有)。
 * 副作用: 可能写 *c 两个字段并输出一条 ERROR 日志。 */
static void comp_checked(comp_value_t *c)
{
    if ((c->x < COMP_VALUE_MIN) || (c->x > COMP_VALUE_MAX) ||
        (c->y < COMP_VALUE_MIN) || (c->y > COMP_VALUE_MAX))
    {
        c->x = PARAM_DEF_COMP;
        c->y = PARAM_DEF_COMP;
        LOG_SYS_ERROR("param comp out of range, fallback default");
    }
}

/* param_apply —— 把第 idx 套应用到硬件与 ISR 镜像
 * 目的: 套切换或整包写后, 让 s_eff/s_comp/硬件码值与新套保持一致。
 * 输入: idx —— 套索引, 调用方保证 < PARAM_PROFILE_NUM。
 * 输出: 更新 s_eff 与 s_comp 镜像, 并写 AD5290 码值。
 * 返回值: 无。
 * 调用关系: param_init / param_profile_set / param_active_set /
 *   param_force_set / param_force_clear 内部调用(本文件私有)。
 * 副作用: 写模块私有态 s_eff/s_comp; 经 ad5290_set_all_code 一次拉低 CS
 *   同步写 6 路码值(硬件写入)。 */
static void param_apply(uint8_t idx)
{
    s_eff     = idx;
    s_comp.x  = s_store.sets[idx].comp.x;
    s_comp.y  = s_store.sets[idx].comp.y;
    ad5290_set_all_code((const uint8_t *)&s_store.sets[idx].radc);
}

/* param_comp —— 取生效套 comp 的只读指针
 * 目的: 供 1MHz 管线 ISR 每拍直读补偿值, 零开销(不拷贝结构体)。
 * 输入: 无。
 * 输出: 无(不写任何状态)。
 * 返回值: 生效套 comp 镜像的只读指针, 恒非 NULL。
 * 调用关系: ad_da_alg.c 线性算法 ISR 每拍调用; 指向本文件 s_comp。
 * 副作用: 无(只读)。执行上下文: 主循环域与 ISR 域共调, 无 static 局部,
 *   可重入; 最坏代价为一次取址, 不含分支与循环。 */
const volatile comp_value_t *param_comp(void)
{
    return &s_comp;
}

/* param_set_comp_x —— 写生效套 X 补偿值(带范围校验)
 * 目的: 协议层单点改写生效套的 X 补偿, 同步 ISR 镜像。
 * 输入: v —— 目标 X 补偿值, 合法域 [COMP_VALUE_MIN, COMP_VALUE_MAX]。
 * 输出: 写 s_store 生效套的 comp.x 与 s_comp.x。
 * 返回值: 0 成功; -1 越界拒收(状态不变)。
 * 调用关系: 协议层经 param.h 调用。
 * 副作用: 写模块私有态 s_store/s_comp(不写硬件)。 */
int param_set_comp_x(int16_t v)
{
    if ((v < COMP_VALUE_MIN) || (v > COMP_VALUE_MAX))
    {
        LOG_SYS_ERROR("comp x out of range: %d", v);
        return -1;
    }
    s_store.sets[s_eff].comp.x = v;
    s_comp.x = v;
    return 0;
}

/* param_set_comp_y —— 写生效套 Y 补偿值(带范围校验)
 * 目的: 协议层单点改写生效套的 Y 补偿, 同步 ISR 镜像。
 * 输入: v —— 目标 Y 补偿值, 合法域 [COMP_VALUE_MIN, COMP_VALUE_MAX]。
 * 输出: 写 s_store 生效套的 comp.y 与 s_comp.y。
 * 返回值: 0 成功; -1 越界拒收(状态不变)。
 * 调用关系: 协议层经 param.h 调用。
 * 副作用: 写模块私有态 s_store/s_comp(不写硬件)。 */
int param_set_comp_y(int16_t v)
{
    if ((v < COMP_VALUE_MIN) || (v > COMP_VALUE_MAX))
    {
        LOG_SYS_ERROR("comp y out of range: %d", v);
        return -1;
    }
    s_store.sets[s_eff].comp.y = v;
    s_comp.y = v;
    return 0;
}

/* param_radc_get —— 取生效套的电位器码值快照
 * 目的: 供协议层读全部 6 路码值后做单通道改写。
 * 输入: 无。
 * 输出: 无。
 * 返回值: 生效套 radc 的结构体拷贝(主循环域, 非 ISR 路径)。
 * 调用关系: 协议层经 param.h 调用。
 * 副作用: 无(只读 s_store)。 */
radc_value_t param_radc_get(void)
{
    return s_store.sets[s_eff].radc;
}

/* param_radc_set —— 整组写生效套的电位器码值
 * 目的: 协议层改单通道后回写整组码值。
 * 输入: r —— 6 路码值; NULL 时拒收。
 * 输出: 写 s_store 生效套的 radc。
 * 返回值: 无。
 * 调用关系: 协议层经 param.h 调用。
 * 副作用: 写模块私有态 s_store; 硬件写入由调用方另经
 *   ad5290_set_code / ad5290_set_all_code 完成, 本函数不碰硬件。 */
void param_radc_set(const radc_value_t *r)
{
    if (r == NULL)
    {
        return;
    }
    s_store.sets[s_eff].radc = *r;
}

/* param_save —— 持久化三套配置
 * 目的: 把三套配置整包落 flash(长度变更即视为旧布局, 由 bsp_flash 拒收)。
 * 输入: 无。
 * 输出: 无(数据经 bsp_flash 落存储)。
 * 返回值: 0 成功; -1 失败(存储层错误)。
 * 调用关系: 协议层经 param.h 调用; 内部调 bsp_flash_save。
 * 副作用: 经 bsp_flash_save 擦写参数存储区(外部非易失存储副作用);
 *   强制态 s_forced 为易失态, 不落盘。 */
int param_save(void)
{
    return bsp_flash_save(&s_store, sizeof(s_store));
}

/* param_active —— 取自主生效套索引
 * 目的: 供协议层/看门狗查询当前自主生效套。
 * 输入: 无。
 * 输出: 无。
 * 返回值: 自主生效套索引, 恒 < PARAM_PROFILE_NUM。
 * 调用关系: 协议层与强制态看门狗调用。
 * 副作用: 无(只读 s_active)。 */
uint8_t param_active(void)
{
    return s_active;
}

/* param_forced —— 取强制套索引
 * 目的: 供协议层/看门狗判断当前是否处于强制态。
 * 输入: 无。
 * 输出: 无。
 * 返回值: 强制套索引; PARAM_PROFILE_NONE = 未强制。
 * 调用关系: 协议层与强制态看门狗调用。
 * 副作用: 无(只读 s_forced)。 */
uint8_t param_forced(void)
{
    return s_forced;
}

/* param_profile —— 取指定套的只读视图
 * 目的: 供协议层全量回读三套配置。
 * 输入: idx —— 套索引。
 * 输出: 无。
 * 返回值: 指向 s_store 中该套的只读指针(内部存储活视图, 非拷贝; 调用方
 *   不得跨写操作持有); idx 越界返 NULL。
 * 调用关系: 协议层经 param.h 调用。
 * 副作用: 无(只读 s_store)。 */
const param_profile_t *param_profile(uint8_t idx)
{
    if (idx >= PARAM_PROFILE_NUM)
    {
        LOG_SYS_ERROR("param profile idx out of range: %u", idx);
        return NULL;
    }
    return &s_store.sets[idx];
}

/* param_profile_set —— 整包写指定套(带 comp 范围一致性检查)
 * 目的: 协议层整包下发一套配置时, 落存储并做范围检查。
 * 输入: idx —— 套索引; p —— 待写入的一套配置, NULL 拒收。
 * 输出: 写 s_store.sets[idx]; 越界 comp 整体回退默认。
 * 返回值: 0 成功; -1 索引越界或 p 为空。
 * 调用关系: 协议层经 param.h 调用; 写的是生效套时内部转 param_apply。
 * 副作用: 写模块私有态 s_store; 若改的是生效套, 经 param_apply 写
 *   AD5290 硬件并刷新 s_comp/s_eff。 */
int param_profile_set(uint8_t idx, const param_profile_t *p)
{
    if ((p == NULL) || (idx >= PARAM_PROFILE_NUM))
    {
        LOG_SYS_ERROR("param profile set invalid: idx=%u", idx);
        return -1;
    }

    s_store.sets[idx] = *p;

    /* comp 一致性检查: 越界整体回退默认(规范 10-2) */
    comp_checked(&s_store.sets[idx].comp);

    /* 若写的正是生效套, 同步应用 */
    if (idx == s_eff)
    {
        param_apply(idx);
    }
    return 0;
}

/* param_active_set —— 设置自主生效套(工况判定模块调用)
 * 目的: 工况判定模块据现场条件切换自主生效套。
 * 输入: idx —— 目标自主生效套索引。
 * 输出: 写 s_active; 未强制时经 param_apply 应用该套。
 * 返回值: 无(越界忽略并告警)。
 * 调用关系: 工况判定模块经 param.h 调用。
 * 副作用: 写模块私有态 s_active; 未强制时写 AD5290 硬件并刷新
 *   s_eff/s_comp; 处于强制态时仅记录索引, 不影响生效套。 */
void param_active_set(uint8_t idx)
{
    if (idx >= PARAM_PROFILE_NUM)
    {
        LOG_SYS_ERROR("param active idx out of range: %u", idx);
        return;
    }
    s_active = idx;

    /* 未强制时才跟随自主 */
    if (s_forced == PARAM_PROFILE_NONE)
    {
        param_apply(idx);
    }
}

/* param_force_set —— 强制到指定套
 * 目的: 上位机/维护流程临时强制生效某套(优先于自主判定)。
 * 输入: idx —— 目标强制套索引。
 * 输出: 写 s_forced 并经 param_apply 应用该套。
 * 返回值: 0 成功; -1 索引越界(状态不变)。
 * 调用关系: 协议层经 param.h 调用。
 * 副作用: 写模块私有态 s_forced/s_eff/s_comp; 写 AD5290 硬件。 */
int param_force_set(uint8_t idx)
{
    if (idx >= PARAM_PROFILE_NUM)
    {
        LOG_SYS_ERROR("param force idx out of range: %u", idx);
        return -1;
    }
    s_forced = idx;
    param_apply(idx);
    return 0;
}

/* param_force_clear —— 解除强制, 回到自主生效套
 * 目的: 退出强制态, 交还工况判定模块的自主选套。
 * 输入: 无。
 * 输出: s_forced 置 PARAM_PROFILE_NONE, 并经 param_apply 应用 s_active。
 * 返回值: 恒 0。
 * 调用关系: 协议层与强制态看门狗经 param.h 调用。
 * 副作用: 写模块私有态 s_forced/s_eff/s_comp; 写 AD5290 硬件。 */
int param_force_clear(void)
{
    s_forced = PARAM_PROFILE_NONE;
    param_apply(s_active);
    return 0;
}

/* param_init —— 参数初始化: 三套加载 + 逐套一致性检查 + 应用第 0 套
 * 目的: 上电/复位时把三套配置装入内存并落到硬件。
 * 输入: 无(数据来自 bsp_flash 或默认值)。
 * 输出: 写 s_store/s_forced/s_active/s_eff/s_comp。
 * 返回值: 无。
 * 调用关系: main.c 启动序列调用; 内部调 bsp_flash_load 与 param_apply。
 * 副作用: 写模块私有态; 经 param_apply 写 AD5290 硬件;
 *   flash 加载的 magic+CRC+长度已由 bsp_flash 校验, 长度不符(旧布局)
 *   即回退默认。 */
void param_init(void)
{
    uint8_t i;   /* 套索引 */

    if (bsp_flash_load(&s_store, sizeof(s_store)) == 0)
    {
        /* 逐套 comp 范围一致性检查: 越界回退默认 */
        for (i = 0U; i < PARAM_PROFILE_NUM; i++)
        {
            comp_checked(&s_store.sets[i].comp);
        }
        LOG_SYS_INFO("load param from flash");
    }
    else
    {
        /* 无有效数据: 三套全部落默认值 */
        for (i = 0U; i < PARAM_PROFILE_NUM; i++)
        {
            s_store.sets[i].radc.x1 = PARAM_DEF_X1;
            s_store.sets[i].radc.x2 = PARAM_DEF_X2;
            s_store.sets[i].radc.x3 = PARAM_DEF_X3;
            s_store.sets[i].radc.y1 = PARAM_DEF_Y1;
            s_store.sets[i].radc.y2 = PARAM_DEF_Y2;
            s_store.sets[i].radc.y3 = PARAM_DEF_Y3;
            s_store.sets[i].comp.x  = PARAM_DEF_COMP;
            s_store.sets[i].comp.y  = PARAM_DEF_COMP;
        }
        LOG_SYS_INFO("load param from default");
    }

    /* 强制态为易失态: 任何上电/复位都从"未强制"开始(spec §6.2) */
    s_forced = PARAM_PROFILE_NONE;
    s_active = 0U;
    param_apply(0U);

    LOG_SYS_INFO("param: active=%u forced=0x%02X", s_active, s_forced);
    LOG_SYS_INFO("===================================================");
}

/* file end */
