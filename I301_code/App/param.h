/* param.h —— 参数层接口(I301 振镜 XY 板)
 * 职责: 参数类型(一套配置 param_profile_t = 电位器码 radc + 补偿 comp)、
 *   Flash 镜像 flash_store_t、合法范围宏、初始化入口与读写接口(实现见
 *   param.c)。
 * 三套模型: 存储恒有三套(0=30k 大角度, 1=40k 小角度, 2=过流降速);
 *   另有自主生效套与强制套两个索引, 生效套 = 强制优先于自主(强制套
 *   不为 PARAM_PROFILE_NONE 时取强制套, 否则取自主套)。既有读写接口
 *   param_radc_get/param_radc_set/param_set_comp_x/param_set_comp_y/
 *   param_comp/param_save 一律作用于当前生效套。
 * 封装: 参数实体为 param.c 私有(static), 外部仅经接口函数访问——
 *   写方单点收敛、范围校验单点收敛; ISR 经 param_comp() 只读指针
 *   直读字段, 零开销。
 * 上下游: main.c 按启动序列调用初始化; 协议层经接口读写参数;
 *   bsp_flash 按不透明 blob 持久化(param_save 内部组装)。 */
#ifndef __PARAM_H__
#define __PARAM_H__

#include <stdint.h>

#pragma pack(1)
typedef struct
{
    uint8_t x1;
    uint8_t x2;
    uint8_t x3;
    uint8_t y1;
    uint8_t y2;
    uint8_t y3;
} radc_value_t;

typedef struct
{
    int16_t x;
    int16_t y;
} comp_value_t;

/* 补偿值合法范围(协议契约与加载一致性检查共用边界) */
#define COMP_VALUE_MIN  (-2000)
#define COMP_VALUE_MAX  2000

/* 一套配置(码值 6 路 + 补偿值 X/Y, 10B)
 * 字节布局须与协议层 ls_profile_t 一致(spec §4.2) */
typedef struct
{
    radc_value_t  radc;           /* 电位器码值 6 路 */
    comp_value_t  comp;           /* 补偿值 X/Y      */
} param_profile_t;

/* 配置套数: 0=30k大角度 1=40k小角度 2=过流降速(spec §3.1) */
#define PARAM_PROFILE_NUM   3
#define PARAM_PROFILE_NONE  0xFF   /* 未强制 */

/* Flash 持久化存储总结构体(30B ≤ BSP_FLASH_DATA_MAX=32)
 * 布局变更 → 旧数据长度不符 → 一次性回退默认(param_init) */
typedef struct
{
    param_profile_t sets[PARAM_PROFILE_NUM];
} flash_store_t;
#pragma pack()

/* ---- 读写接口(参数唯一对外访问入口, 均作用于当前生效套) ----
 * param_save 经 bsp_flash 以 magic+CRC 整包持久化;
 * param_radc_set 只改内存, 硬件写入由调用方另经 ad5290_set_code。 */
const volatile comp_value_t *param_comp(void); /* comp 只读指针: ISR 每拍直读字段, 零开销 */
int param_set_comp_x(int16_t v);               /* 带范围校验写: 0 成功, -1 越界拒收 */
int param_set_comp_y(int16_t v);               /* 带范围校验写: 0 成功, -1 越界拒收 */
radc_value_t param_radc_get(void);             /* 电位器码值快照(结构体拷贝, 主循环域) */
void param_radc_set(const radc_value_t *r);    /* 写回生效套(硬件写入见上) */
int param_save(void);                          /* 持久化三套参数: 0 成功, -1 失败 */

/* ---- 三套配置接口(索引越界一律拒收: 读接口返 NULL, 写/强制返 -1) ----
 * param_profile_set 为整包写, 返回 0 成功 / -1 越界或空指针;
 * param_active_set 由工况判定模块调用, 越界忽略;
 * 强制态为易失态(不落 flash), param_init 后恒为未强制。 */
const param_profile_t *param_profile(uint8_t idx);   /* 只读快照 */
int  param_profile_set(uint8_t idx, const param_profile_t *p); /* 整包写 */
int  param_force_set(uint8_t idx);                   /* 强制到该套 */
int  param_force_clear(void);                        /* 解除强制 */
uint8_t param_active(void);                          /* 自主生效套 */
uint8_t param_forced(void);                          /* 强制套, NONE=未强制 */
void param_active_set(uint8_t idx);                  /* 工况判定模块调用 */

void ad5290_set_init(void);  /* AD5290 电位器初始化包装(main.c 启动序列调用) */
void param_init(void);       /* 三套加载(长度/范围违例回退默认)并应用第 0 套 */

#endif /* __PARAM_H__ */
