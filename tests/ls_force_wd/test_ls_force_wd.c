/* test_ls_force_wd.c —— 强制套超时判定宿主单测(规范 8-1/8-2)
 * 构建/运行: sh tests/ls_force_wd/run.sh
 * 被测: ls_force_wd_expired()(定义于 ls_proto_device_app.h 的纯函数)
 * 本测只含头文件, 不链接任何固件源文件, 无平台依赖。
 * 覆盖(边界优先):
 *   1. 常规边界: 未到 / 恰好等于 / 刚过 超时时长;
 *   2. uint32 毫秒回绕(规范 8-2 明列必测): 跨回绕的差值须等于真实间隔,
 *      既不误判超时也不漏判;
 *   3. 冷启动: last = 0 的初值行为。 */
#include <stdio.h>
#include <stdint.h>

#include "ls_proto_device_app.h"

static int s_fail = 0;
static int s_pass = 0;

#define CHECK_EQ(got, want, msg)                                        \
    do                                                                  \
    {                                                                   \
        if ((long)(got) == (long)(want))                                \
        {                                                               \
            s_pass++;                                                   \
        }                                                               \
        else                                                            \
        {                                                               \
            s_fail++;                                                   \
            printf("FAIL(line %d): %s got=%ld want=%ld\n",              \
                   __LINE__, (msg), (long)(got), (long)(want));         \
        }                                                               \
    } while (0)

int main(void)
{
    /* 1. 常规边界(阈值 5000ms, 判据是"严格大于") */
    CHECK_EQ(ls_force_wd_expired(1000U, 0U), 0, "1s not expired");
    CHECK_EQ(ls_force_wd_expired(4999U, 0U), 0, "4999ms not expired");
    CHECK_EQ(ls_force_wd_expired(5000U, 0U), 0, "exactly 5000 not expired");
    CHECK_EQ(ls_force_wd_expired(5001U, 0U), 1, "5001ms expired");

    /* 2. uint32 回绕: last 在回绕前, now 在回绕后 */
    /* last=0xFFFFFF00, now=0x00000100 → 真实间隔 0x200 = 512ms */
    CHECK_EQ(ls_force_wd_expired(0x00000100U, 0xFFFFFF00U), 0,
             "wrap 512ms not expired");
    /* last=0xFFFFFF00, now=0x00001400 → 真实间隔 0x1500 = 5376ms */
    CHECK_EQ(ls_force_wd_expired(0x00001400U, 0xFFFFFF00U), 1,
             "wrap 5376ms expired");
    /* now 恰为 0(回绕瞬间), last=0xFFFFF000 → 真实间隔 4096ms */
    CHECK_EQ(ls_force_wd_expired(0U, 0xFFFFF000U), 0,
             "wrapped to zero, 4096ms");
    /* now 刚过回绕零点 → 真实间隔 5632ms */
    CHECK_EQ(ls_force_wd_expired(0x00000600U, 0xFFFFF000U), 1,
             "wrapped past zero, 5632ms");

    /* 3. 冷启动: last 初值 0 */
    CHECK_EQ(ls_force_wd_expired(0U, 0U), 0, "cold start same tick");
    CHECK_EQ(ls_force_wd_expired(6000U, 0U), 1, "cold start expired");

    printf("ls_force_wd: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
