/* test_ad_da_alg.c —— ad_da_process_linear 宿主单测(TDD, 规范 8-1/8-2)
 * 构建/运行: sh tests/ad_da_alg/run.sh
 * 覆盖(边界优先):
 *   1. IN 反相公式 y=(4095+off)-x、FB 同相公式 y=x+off(off=0);
 *   2. comp 偏置叠加(+/-);
 *   3. 饱和钳位: 上溢→4095、下溢→0;
 *   4. 量程边界样本 0/4095;
 *   5. 奇数样本尾巴(展开余数路径)。 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ad_da.h"
#include "param.h"

/* ---- param 桩: 只实现被测模块用到的接口 ---- */
static comp_value_t s_test_comp;

const volatile comp_value_t *param_comp(void)
{
    return (const volatile comp_value_t *)&s_test_comp;
}

/* ---- 测试脚手架 ---- */
static int s_fail = 0;
static int s_pass = 0;

#define CHECK_EQ(got, want, msg)                                       \
    do                                                                 \
    {                                                                  \
        if ((uint16_t)(got) == (uint16_t)(want))                       \
        {                                                              \
            s_pass++;                                                  \
        }                                                              \
        else                                                           \
        {                                                              \
            s_fail++;                                                  \
            printf("FAIL(line %d): %s got=%u want=%u\n",               \
                   __LINE__, (msg), (unsigned)(uint16_t)(got),         \
                   (unsigned)(uint16_t)(want));                        \
        }                                                              \
    } while (0)

/* 以常量样本填满 4 通道输入, 执行算法, 逐通道校验首样本 */
static void run_case(int16_t comp_x, int16_t comp_y,
                     uint16_t vx, uint16_t ix, uint16_t vy, uint16_t iy,
                     uint16_t want_inx, uint16_t want_fbx,
                     uint16_t want_iny, uint16_t want_fby,
                     uint16_t n)
{
    /* 每通道独立缓冲(栈上小尺寸, n ≤ 16) */
    uint16_t b0[16], b1[16], b2[16], b3[16];
    uint16_t o0[16], o1[16], o2[16], o3[16];
    const uint16_t *in[AD_DA_CH_NUM];
    uint16_t       *out[AD_DA_CH_NUM];
    uint16_t        i;

    s_test_comp.x = comp_x;
    s_test_comp.y = comp_y;

    for (i = 0U; i < n; i++)
    {
        b0[i] = vx;
        b1[i] = ix;
        b2[i] = vy;
        b3[i] = iy;
        o0[i] = 0xEEEEU;
        o1[i] = 0xEEEEU;
        o2[i] = 0xEEEEU;
        o3[i] = 0xEEEEU;
    }

    in[0] = b0;  out[0] = o0;
    in[1] = b1;  out[1] = o1;
    in[2] = b2;  out[2] = o2;
    in[3] = b3;  out[3] = o3;

    ad_da_process_linear(in, out, n);

    CHECK_EQ(o0[0], want_inx, "IN-X");
    CHECK_EQ(o1[0], want_fbx, "FB-X");
    CHECK_EQ(o2[0], want_iny, "IN-Y");
    CHECK_EQ(o3[0], want_fby, "FB-Y");
    /* 展开尾样本同值校验(覆盖奇数尾巴) */
    CHECK_EQ(o0[n - 1U], want_inx, "IN-X tail");
    CHECK_EQ(o1[n - 1U], want_fbx, "FB-X tail");
}

int main(void)
{
    /* 1. off=0 基准: IN 反相、FB 恒等 */
    run_case(0, 0,  100, 200, 300, 400,
             3995, 200, 3795, 400, 8);

    /* 2. comp 偏置叠加 */
    run_case(100, -50,  1000, 1000, 1000, 1000,
             4095 - 1000 + 100, 1000 + 100,     /* X: IN 加 off、FB 加 off */
             4095 - 1000 - 50,  1000 - 50,      /* Y */
             8);

    /* 3. 饱和钳位: 上溢 */
    run_case(2000, 2000,  0, 4000, 0, 4000,
             4095, 4095, 4095, 4095, 8);        /* IN: 4095+2000-0=6095→4095;
                                                    FB: 4000+2000=6000→4095 */

    /* 4. 饱和钳位: 下溢 */
    run_case(-2000, -2000,  4095, 0, 4095, 0,
             0, 0, 0, 0, 8);                    /* IN: 4095-2000-4095=-2000→0;
                                                    FB: 0-2000→0 */

    /* 5. 量程边界样本 + off=0 */
    run_case(0, 0,  0, 0, 4095, 4095,
             4095, 0, 0, 4095, 8);

    /* 6. 奇数样本尾巴(展开余数路径) */
    run_case(0, 0,  1, 2, 3, 4,
             4094, 2, 4092, 4, 5);
    run_case(7, 7,  10, 20, 30, 40,
             4095 - 10 + 7, 20 + 7, 4095 - 30 + 7, 40 + 7, 1);

    printf("ad_da_alg: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
