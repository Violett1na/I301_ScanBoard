/* test_ocd_sig.c — 过流 PWM 信令纯状态机(ocd_sig.h)宿主单元测试
 *
 * 被测对象: I301_code/Core/Inc/ocd_sig.h 的 ocd_sig_axis_init / ocd_sig_feed
 *   —— 与固件 ocd_sig.c 编译同一份代码, 非复制品。
 * 语义(2026-08-27-ocd-pwm-signaling-design.md §4):
 *   peg = 样本==0 或 ==4095; SIG_OFF 下连续 peg > OCD_SIG_TRIP_RUN(8) 跳闸;
 *   非 peg 清零连续计数; SIG_ON 下 peg 清零释放计时, 无 peg 稳定持续
 *   OCD_SIG_RELEASE_MS(1000) 释放。
 *
 * 构建运行(WSL/Linux):
 *   gcc -std=c99 -Wall -Wextra -O2 -I ../../I301_code/Core/Inc \
 *       test_ocd_sig.c -o test_ocd_sig && ./test_ocd_sig
 */
#include <stdio.h>
#include <stdint.h>
#include "ocd_sig.h"

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { g_pass++; }                                         \
        else {                                                          \
            g_fail++;                                                   \
            printf("  失败: %s (%s:%d)\n", (msg), __FILE__, __LINE__);  \
        }                                                               \
    } while (0)

/* ---- A 组: 跳闸与 peg 边界 ---- */

/* 1) 连续 8 个 peg 不跳, 第 9 个跳(阈值 >8, 同 ocd.c s_run > OCD_TRIP_RUN) */
static void test_trip_after_run_gt8(void)
{
    ocd_sig_axis_t ax;
    ocd_sig_axis_init(&ax);

    for (int i = 0; i < 8; i++)
    {
        CHECK(ocd_sig_feed(&ax, 0U, 0U) == OCD_SIG_EV_NONE, "≤8 连续 peg 不应跳闸");
    }
    CHECK(ax.on == 0U, "8 连续 peg 后仍应 OFF");
    CHECK(ocd_sig_feed(&ax, 0U, 0U) == OCD_SIG_EV_TRIP, "第 9 个连续 peg 应跳闸");
    CHECK(ax.on == 1U, "跳闸后状态应为 ON");
    CHECK(ax.trips == 1U, "跳闸计数应为 1");
}

/* 2) peg 边界: 0/4095 是 peg; 1/4094 非 peg(永不跳) */
static void test_peg_boundary_values(void)
{
    ocd_sig_axis_t ax;
    ocd_sig_axis_init(&ax);

    for (int i = 0; i < 8; i++)
    {
        ocd_sig_feed(&ax, 4095U, 0U);
    }
    CHECK(ocd_sig_feed(&ax, 4095U, 0U) == OCD_SIG_EV_TRIP, "4095 应计 peg 并可跳闸");

    ocd_sig_axis_init(&ax);
    for (int i = 0; i < 100; i++)
    {
        uint16_t v = (i % 2 == 0) ? 1U : 4094U;
        CHECK(ocd_sig_feed(&ax, v, 0U) == OCD_SIG_EV_NONE, "1/4094 不应计 peg");
    }
    CHECK(ax.on == 0U, "全非 peg 流不应跳闸");
}

/* 3) 非 peg 清零连续计数: 8 peg + 1 非 peg + 8 peg 不跳, 再来 1 个才跳 */
static void test_run_reset_by_non_peg(void)
{
    ocd_sig_axis_t ax;
    ocd_sig_axis_init(&ax);

    for (int i = 0; i < 8; i++)
    {
        ocd_sig_feed(&ax, 0U, 0U);
    }
    CHECK(ocd_sig_feed(&ax, 2048U, 0U) == OCD_SIG_EV_NONE, "非 peg 不应跳闸");
    for (int i = 0; i < 8; i++)
    {
        CHECK(ocd_sig_feed(&ax, 0U, 0U) == OCD_SIG_EV_NONE, "重新计数 ≤8 不应跳");
    }
    CHECK(ocd_sig_feed(&ax, 0U, 0U) == OCD_SIG_EV_TRIP, "重数后第 9 连续 peg 应跳");
}

/* ---- B 组: 防抖释放 ---- */

/* 4) 跳闸后无 peg 稳定满 1s 释放; 999ms 不释放 */
static void test_release_after_1s_clean(void)
{
    ocd_sig_axis_t ax;
    ocd_sig_axis_init(&ax);

    for (int i = 0; i < 9; i++)
    {
        ocd_sig_feed(&ax, 0U, 0U);
    }
    CHECK(ax.on == 1U, "应已跳闸");

    /* 首个非 peg 样本与跳闸同 tick: 开始计时 */
    CHECK(ocd_sig_feed(&ax, 2048U, 0U) == OCD_SIG_EV_NONE, "首个非 peg 只开始计时");
    for (uint32_t t = 1U; t <= 999U; t++)
    {
        if (ocd_sig_feed(&ax, 2048U, t) != OCD_SIG_EV_NONE)
        {
            CHECK(0, "未满 1s 不应释放");
        }
    }
    CHECK(ax.on == 1U, "999ms 仍应 ON");
    CHECK(ocd_sig_feed(&ax, 2048U, 1000U) == OCD_SIG_EV_RELEASE, "无 peg 满 1s 应释放");
    CHECK(ax.on == 0U, "释放后状态应为 OFF");
}

/* 5) 释放期内再现 peg: 清零重计, 需重新稳定满 1s */
static void test_release_reset_by_peg(void)
{
    ocd_sig_axis_t ax;
    ocd_sig_axis_init(&ax);

    for (int i = 0; i < 9; i++)
    {
        ocd_sig_feed(&ax, 0U, 0U);
    }
    ocd_sig_feed(&ax, 2048U, 0U);              /* 开始计时 */
    for (uint32_t t = 1U; t <= 500U; t++)
    {
        ocd_sig_feed(&ax, 2048U, t);
    }
    CHECK(ocd_sig_feed(&ax, 4095U, 501U) == OCD_SIG_EV_NONE, "再现 peg 不应释放");
    for (uint32_t t = 502U; t <= 1501U; t++)
    {
        if (ocd_sig_feed(&ax, 2048U, t) != OCD_SIG_EV_NONE)
        {
            CHECK(0, "重计未满 1s 不应释放");
        }
    }
    CHECK(ax.on == 1U, "重计 999ms 仍应 ON");
    CHECK(ocd_sig_feed(&ax, 2048U, 1502U) == OCD_SIG_EV_RELEASE, "重计满 1s 应释放");
}

/* 6) 持续钉轨(故障未除): 永不释放, 方波持续报警 */
static void test_no_release_while_pegged(void)
{
    ocd_sig_axis_t ax;
    ocd_sig_axis_init(&ax);

    for (int i = 0; i < 9; i++)
    {
        ocd_sig_feed(&ax, 0U, 0U);
    }
    uint8_t ev = OCD_SIG_EV_NONE;
    for (uint32_t t = 1U; t <= 5000U; t++)
    {
        ev = ocd_sig_feed(&ax, 4095U, t);
    }
    CHECK(ev == OCD_SIG_EV_NONE, "持续 peg 不应释放");
    CHECK(ax.on == 1U, "持续 peg 应保持 ON");
}

/* ---- C 组: 双轴独立与再触发 ---- */

/* 7) 两轴状态机互不影响 */
static void test_axes_independent(void)
{
    ocd_sig_axis_t ax_x;
    ocd_sig_axis_t ax_y;
    ocd_sig_axis_init(&ax_x);
    ocd_sig_axis_init(&ax_y);

    for (int i = 0; i < 9; i++)
    {
        ocd_sig_feed(&ax_x, 0U, 0U);      /* X 连续 peg */
        ocd_sig_feed(&ax_y, 2048U, 0U);   /* Y 正常 */
    }
    CHECK(ax_x.on == 1U, "X 应跳闸");
    CHECK(ax_y.on == 0U, "Y 不应受 X 影响");

    for (int i = 0; i < 9; i++)
    {
        ocd_sig_feed(&ax_y, 4095U, 1U);   /* Y 独立跳闸 */
    }
    CHECK(ax_y.on == 1U, "Y 应独立跳闸");
    CHECK(ax_x.on == 1U, "X 状态不应受影响");
}

/* 8) 释放后再触发: 需重新满窗, 跳闸计数累计 */
static void test_retrip_after_release(void)
{
    ocd_sig_axis_t ax;
    ocd_sig_axis_init(&ax);

    for (int i = 0; i < 9; i++)
    {
        ocd_sig_feed(&ax, 0U, 0U);
    }
    ocd_sig_feed(&ax, 2048U, 0U);
    CHECK(ocd_sig_feed(&ax, 2048U, 1000U) == OCD_SIG_EV_RELEASE, "应释放");

    for (int i = 0; i < 8; i++)
    {
        CHECK(ocd_sig_feed(&ax, 0U, 1001U) == OCD_SIG_EV_NONE, "再触发需重新满窗");
    }
    CHECK(ocd_sig_feed(&ax, 0U, 1001U) == OCD_SIG_EV_TRIP, "第 9 连续 peg 再跳闸");
    CHECK(ax.trips == 2U, "跳闸计数累计为 2");
}

int main(void)
{
    test_trip_after_run_gt8();
    test_peg_boundary_values();
    test_run_reset_by_non_peg();
    test_release_after_1s_clean();
    test_release_reset_by_peg();
    test_no_release_while_pegged();
    test_axes_independent();
    test_retrip_after_release();

    printf("ocd_sig 宿主单测: %d 通过, %d 失败\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
