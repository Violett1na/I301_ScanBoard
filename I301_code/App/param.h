/* param.h —— 参数层接口(I301 振镜 XY 板)
 * 职责: 参数类型(电位器码 radc / 补偿 comp / Flash 镜像 flash_store_t)、
 *   合法范围宏、初始化入口与读写接口(实现见 param.c)。
 * 封装: 三个参数实体为 param.c 私有(static), 外部仅经接口函数访问——
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

/* Flash 持久化存储总结构体，所有需要保存到 Flash 的参数统一放入此处
 * (经 bsp_flash 不透明 blob 接口存取, 存储层不认识本类型) */
typedef struct
{
    radc_value_t  radc;           /* 电位器码值 */
    comp_value_t  comp;           /* 补偿值 */
} flash_store_t;
#pragma pack()

/* ---- 读写接口(参数唯一对外访问入口) ---- */
const volatile comp_value_t *param_comp(void); /* comp 只读指针: ISR 每拍直读字段, 零开销 */
int param_set_comp_x(int16_t v);               /* 带范围校验写: 0 成功, -1 越界拒收 */
int param_set_comp_y(int16_t v);               /* 带范围校验写: 0 成功, -1 越界拒收 */
radc_value_t param_radc_get(void);             /* 电位器码值快照(结构体拷贝, 主循环域) */
void param_radc_set(const radc_value_t *r);    /* 电位器码值写回; 硬件写入由调用方另经 ad5290_set_code */
int param_save(void);                          /* 快照当前参数并持久化(magic+CRC): 0 成功, -1 失败 */

void ad5290_set_init(void);  /* AD5290 电位器初始化包装(main.c 启动序列调用) */
void param_init(void);       /* flash 参数加载(违例回退默认+范围一致性检查)并写电位器 */

#endif /* __PARAM_H__ */
