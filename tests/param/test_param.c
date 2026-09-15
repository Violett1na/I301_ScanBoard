/* test_param.c —— param 参数层宿主单测(TDD, 规范 8-1/8-2)
 * 构建/运行: sh tests/param/run.sh
 * 覆盖(边界优先):
 *   1. 三套存储互不串扰(写第 2 套不动第 0/1 套);
 *   2. 索引越界拒收;
 *   3. comp 范围越界回退默认, 且阈值等值含端点(规范 8-2);
 *   4. 生效套切换后 param_radc_get/param_comp 跟随;
 *   5. 强制优先于自主生效;
 *   6. flash_store_t 尺寸 = 30 字节;
 *   7. 硬件落地: 切套/整包写/上电应用后 AD5290 码值跟随(6 路并行写);
 *   8. 强制态易失: 任何 param_init 后复位为未强制。
 * 说明: 本文件为宿主测试替身, 以桩替代 ad5290 / bsp_flash / port_tick /
 *   mylog 四个模块; 桩签名与真实头文件逐字一致。 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "param.h"
#include "ad5290.h"

/* ---- 桩: ad5290 ---- */
static uint8_t s_pot[AD5290_TOTAL_NUM];  /* 码值镜像, 索引=axis*3+ch */
static int     s_pot_all_n = 0;          /* set_all_code 调用次数 */

void ad5290_init(void)
{
}

void ad5290_set_code(ad5290_axis_e axis, ad5290_ch_e ch, uint8_t code)
{
    s_pot[(int)axis * AD5290_CH_PER_AXIS + (int)ch] = code;
}

void ad5290_set_all_code(const uint8_t codes[AD5290_TOTAL_NUM])
{
    memcpy(s_pot, codes, AD5290_TOTAL_NUM);
    s_pot_all_n++;
}

void ad5290_set_ohm(ad5290_axis_e axis, ad5290_ch_e ch, float ohm)
{
    (void)axis;
    (void)ch;
    (void)ohm;
}

void ad5290_set_all_ohm(const float ohms[AD5290_TOTAL_NUM])
{
    (void)ohms;
}

uint8_t ad5290_get_code(ad5290_axis_e axis, ad5290_ch_e ch)
{
    return s_pot[(int)axis * AD5290_CH_PER_AXIS + (int)ch];
}

/* ---- 桩: port_tick ---- */
void port_delay_ms(uint32_t ms)
{
    (void)ms;
}

/* ---- 桩: 日志(LOG_ENABLE_SYS=1 会引用) ---- */
typedef enum
{
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE
} log_level_e;

void log_output(log_level_e level, const char *module, const char *fmt, ...)
{
    (void)level;
    (void)module;
    (void)fmt;
}

void log_output_hex(log_level_e level, const char *module, const char *title,
                    const uint8_t *buf, uint16_t len)
{
    (void)level;
    (void)module;
    (void)title;
    (void)buf;
    (void)len;
}

/* ---- 桩: bsp_flash(内存镜像, 可注入"无有效数据") ---- */
static uint8_t  s_flash[128];        /* flash 数据区镜像(容量大于 30B) */
static uint16_t s_flash_len = 0;     /* 上次保存长度, 用于模拟旧布局 */
static int      s_flash_valid = 0;   /* 0=无有效数据 */

int bsp_flash_save(const void *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U) || (len > sizeof(s_flash)))
    {
        return -1;
    }
    memcpy(s_flash, data, len);
    s_flash_len   = len;
    s_flash_valid = 1;
    return 0;
}

int bsp_flash_load(void *data, uint16_t len)
{
    if (!s_flash_valid || (s_flash_len != len))
    {
        return -1;
    }
    memcpy(data, s_flash, len);
    return 0;
}

/* ---- 测试脚手架 ---- */
static int s_fail = 0;   /* 失败计数 */
static int s_pass = 0;   /* 通过计数 */

/* 比较用 (long long): 64 位宿主上 long 仅 32 位, 指针比较会被
 * -Wpointer-to-int-cast 告警(规范 11-1 零警告); long long 与指针同宽。 */
#define CHECK_EQ(got, want, msg)                                        \
    do                                                                  \
    {                                                                   \
        if ((long long)(got) == (long long)(want))                      \
        {                                                               \
            s_pass++;                                                   \
        }                                                               \
        else                                                            \
        {                                                               \
            s_fail++;                                                   \
            printf("FAIL(line %d): %s got=%lld want=%lld\n",            \
                   __LINE__, (msg), (long long)(got), (long long)(want)); \
        }                                                               \
    } while (0)

static param_profile_t make_profile(uint8_t base, int16_t cx, int16_t cy)
{
    param_profile_t p;   /* 待返回的一套配置 */

    p.radc.x1 = (uint8_t)(base + 1);
    p.radc.x2 = (uint8_t)(base + 2);
    p.radc.x3 = (uint8_t)(base + 3);
    p.radc.y1 = (uint8_t)(base + 4);
    p.radc.y2 = (uint8_t)(base + 5);
    p.radc.y3 = (uint8_t)(base + 6);
    p.comp.x  = cx;
    p.comp.y  = cy;
    return p;
}

static void test_size(void)
{
    CHECK_EQ(sizeof(param_profile_t), 10, "sizeof param_profile_t");
    CHECK_EQ(sizeof(flash_store_t),
             PARAM_PROFILE_NUM * 10, "sizeof flash_store_t");
    CHECK_EQ(PARAM_PROFILE_NUM, 3, "profile num");
}

static void test_three_sets_isolated(void)
{
    param_profile_t p0 = make_profile(10,  -10,  -20);   /* 第 0 套 */
    param_profile_t p1 = make_profile(40,   30,   40);   /* 第 1 套 */
    param_profile_t p2 = make_profile(70, -100, -200);   /* 第 2 套 */
    int             n0;                                  /* 整包写前并行写计数 */

    s_flash_valid = 0;
    param_init();

    n0 = s_pot_all_n;
    CHECK_EQ(param_profile_set(0, &p0), 0, "set p0");
    CHECK_EQ(param_profile_set(1, &p1), 0, "set p1");
    CHECK_EQ(param_profile_set(2, &p2), 0, "set p2");

    /* 整包写落在生效套(第 0 套)上, 硬件跟随: X1=11, Y1=14 */
    CHECK_EQ(ad5290_get_code(AD5290_AXIS_X, AD5290_CH_1), 11,
             "hw x1 follows pack write");
    CHECK_EQ(ad5290_get_code(AD5290_AXIS_Y, AD5290_CH_1), 14,
             "hw y1 follows pack write");
    /* 仅生效套那一次落硬件, 且走 6 路并行写 */
    CHECK_EQ(s_pot_all_n, n0 + 1, "one 6ch parallel write");

    /* 三套互不串扰 */
    CHECK_EQ(param_profile(0)->radc.x1, 11, "p0 x1");
    CHECK_EQ(param_profile(1)->radc.x1, 41, "p1 x1");
    CHECK_EQ(param_profile(2)->radc.x1, 71, "p2 x1");
    CHECK_EQ(param_profile(0)->comp.y,  -20, "p0 comp_y");
    CHECK_EQ(param_profile(2)->comp.y, -200, "p2 comp_y");

    /* 索引越界拒收 */
    CHECK_EQ(param_profile_set(PARAM_PROFILE_NUM, &p0), -1, "idx too big");
    CHECK_EQ(param_profile(PARAM_PROFILE_NUM), NULL, "get idx too big");
    CHECK_EQ(param_profile_set(0, NULL), -1, "null profile");
}

static void test_comp_range_and_default(void)
{
    param_profile_t p = make_profile(10, 5000, 0);   /* X 越界 */

    s_flash_valid = 0;
    param_init();
    CHECK_EQ(param_profile_set(0, &p), 0, "set p with bad comp");

    /* 越界整体回退默认(param.c 私有宏 PARAM_DEF_COMP = -80) */
    CHECK_EQ(param_profile(0)->comp.x, -80, "comp_x fallback");
    CHECK_EQ(param_profile(0)->comp.y, -80, "comp_y fallback");
}

static void test_active_and_force(void)
{
    param_profile_t p0 = make_profile(10,  -10,  -20);   /* 第 0 套 */
    param_profile_t p1 = make_profile(40,   30,   40);   /* 第 1 套 */
    int             n0;                                  /* 切套前并行写计数 */

    s_flash_valid = 0;
    param_init();

    param_profile_set(0, &p0);
    param_profile_set(1, &p1);

    /* 自主生效套 */
    n0 = s_pot_all_n;
    param_active_set(1);
    CHECK_EQ(param_active(), 1, "active 1");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "not forced");
    CHECK_EQ(param_radc_get().x1, 41, "radc follows active");
    CHECK_EQ(param_comp()->x, 30, "comp follows active");
    /* 切套落地到硬件: 该套 X 第 1 路码值 = 41 */
    CHECK_EQ(ad5290_get_code(AD5290_AXIS_X, AD5290_CH_1), 41,
             "hw x1 follows active");
    CHECK_EQ(s_pot_all_n, n0 + 1, "switch writes 6ch in one shot");

    /* 越界自主套索引: 忽略, 当前生效索引不变 */
    param_active_set(PARAM_PROFILE_NUM);
    CHECK_EQ(param_active(), 1, "active idx too big ignored");

    /* 强制优先 */
    CHECK_EQ(param_force_set(0), 0, "force 0");
    CHECK_EQ(param_forced(), 0, "forced 0");
    CHECK_EQ(param_radc_get().x1, 11, "radc follows forced");
    CHECK_EQ(ad5290_get_code(AD5290_AXIS_X, AD5290_CH_1), 11,
             "hw x1 follows forced");

    /* 强制期间改自主套不生效 */
    param_active_set(1);
    CHECK_EQ(param_radc_get().x1, 11, "forced still wins");

    /* 解除强制回自主 */
    CHECK_EQ(param_force_clear(), 0, "clear");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "cleared");
    CHECK_EQ(param_radc_get().x1, 41, "back to active");
    CHECK_EQ(ad5290_get_code(AD5290_AXIS_X, AD5290_CH_1), 41,
             "hw back to active");

    /* 越界强制拒收 */
    CHECK_EQ(param_force_set(PARAM_PROFILE_NUM), -1, "force idx too big");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "still none");
}

static void test_write_goes_to_effective_set(void)
{
    param_profile_t p0 = make_profile(10, 0, 0);   /* 第 0 套 */
    param_profile_t p1 = make_profile(40, 0, 0);   /* 第 1 套 */
    radc_value_t r;                                /* 读改写的码值快照 */

    s_flash_valid = 0;
    param_init();
    param_profile_set(0, &p0);
    param_profile_set(1, &p1);

    /* 生效第 1 套, 经既有单点写接口改一个通道 */
    param_active_set(1);
    r = param_radc_get();
    r.x1 = 200;
    param_radc_set(&r);

    CHECK_EQ(param_profile(1)->radc.x1, 200, "wrote effective set");
    CHECK_EQ(param_profile(0)->radc.x1, 11,  "other set untouched");

    /* comp 同理 */
    CHECK_EQ(param_set_comp_x(-500), 0, "set comp x");
    CHECK_EQ(param_profile(1)->comp.x, -500, "comp wrote effective set");
    CHECK_EQ(param_profile(0)->comp.x,    0, "comp other set untouched");

    /* 越界 comp 拒收 */
    CHECK_EQ(param_set_comp_x(9999), -1, "comp out of range");
}

/* 阈值边界(规范 8-2【必须】): 端点含、端外拒/回退 */
static void test_comp_boundary(void)
{
    param_profile_t p;   /* 整包写用的一套配置 */

    s_flash_valid = 0;
    param_init();

    /* 单点 setter: [COMP_VALUE_MIN, COMP_VALUE_MAX] = [-2000, 2000] 含端点 */
    CHECK_EQ(param_set_comp_x(2000), 0, "comp x max accepted");
    CHECK_EQ(param_set_comp_x(2001), -1, "comp x max+1 rejected");
    CHECK_EQ(param_set_comp_x(-2000), 0, "comp x min accepted");
    CHECK_EQ(param_set_comp_x(-2001), -1, "comp x min-1 rejected");
    CHECK_EQ(param_set_comp_y(2000), 0, "comp y max accepted");
    CHECK_EQ(param_set_comp_y(2001), -1, "comp y max+1 rejected");
    CHECK_EQ(param_set_comp_y(-2000), 0, "comp y min accepted");
    CHECK_EQ(param_set_comp_y(-2001), -1, "comp y min-1 rejected");

    /* 整包写端点: 不被回退默认(param.c 私有宏 PARAM_DEF_COMP = -80) */
    p = make_profile(10, 2000, 0);
    CHECK_EQ(param_profile_set(0, &p), 0, "pack write x=2000");
    CHECK_EQ(param_profile(0)->comp.x, 2000, "x=2000 kept");
    p = make_profile(10, -2000, 0);
    CHECK_EQ(param_profile_set(0, &p), 0, "pack write x=-2000");
    CHECK_EQ(param_profile(0)->comp.x, -2000, "x=-2000 kept");

    /* 整包写端外: 双字段整体回退默认 */
    p = make_profile(10, 2001, 0);
    CHECK_EQ(param_profile_set(0, &p), 0, "pack write x=2001");
    CHECK_EQ(param_profile(0)->comp.x, -80, "x=2001 fallback");
    CHECK_EQ(param_profile(0)->comp.y, -80, "y also fallback");
    p = make_profile(10, -2001, 0);
    CHECK_EQ(param_profile_set(0, &p), 0, "pack write x=-2001");
    CHECK_EQ(param_profile(0)->comp.x, -80, "x=-2001 fallback");
}

/* 上电应用: param_init 后硬件确已写入该套码值 */
static void test_init_applies_hardware(void)
{
    int n0;   /* 第二次 init 前的 6 路并行写计数 */

    s_flash_valid = 0;
    param_init();

    /* param.c 私有默认码值: PARAM_DEF_X1=40 / Y1=40 / X3=45 */
    CHECK_EQ(ad5290_get_code(AD5290_AXIS_X, AD5290_CH_1), 40,
             "init hw x1");
    CHECK_EQ(ad5290_get_code(AD5290_AXIS_Y, AD5290_CH_1), 40,
             "init hw y1");
    CHECK_EQ(ad5290_get_code(AD5290_AXIS_X, AD5290_CH_3), 45,
             "init hw x3");

    /* 上电应用走 6 路并行写(而非逐路), 证明硬件确被写入 */
    n0 = s_pot_all_n;
    param_init();
    CHECK_EQ(s_pot_all_n, n0 + 1, "init applies via one 6ch write");
}

static void test_save_and_load(void)
{
    param_profile_t p0 = make_profile(10,  -10,  -20);   /* 第 0 套 */
    param_profile_t p2 = make_profile(70, -100, -200);   /* 第 2 套 */

    s_flash_valid = 0;
    param_init();
    param_profile_set(0, &p0);
    param_profile_set(2, &p2);

    CHECK_EQ(param_save(), 0, "save");
    CHECK_EQ(s_flash_len, sizeof(flash_store_t), "saved full struct");

    /* 清空内存态后重新加载 */
    param_init();
    CHECK_EQ(param_profile(0)->radc.x1, 11, "reload p0");
    CHECK_EQ(param_profile(2)->radc.x1, 71, "reload p2");
    CHECK_EQ(param_profile(2)->comp.y, -200, "reload p2 comp_y");

    /* 尺寸不符(旧布局) → 回退默认(param.c 私有宏 PARAM_DEF_X1 = 40) */
    s_flash_len = 10;      /* 模拟旧版 10 字节布局 */
    param_init();
    CHECK_EQ(param_profile(0)->radc.x1, 40, "old layout -> default");

    /* 强制态为易失态, 不落 flash: 任何 param_init 后复位(contract) */
    (void)param_force_set(2);
    CHECK_EQ(param_forced(), 2, "forced before reinit");
    param_init();
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "init resets forced");
}

int main(void)
{
    test_size();
    test_three_sets_isolated();
    test_comp_range_and_default();
    test_active_and_force();
    test_write_goes_to_effective_set();
    test_comp_boundary();
    test_init_applies_hardware();
    test_save_and_load();

    printf("param: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
