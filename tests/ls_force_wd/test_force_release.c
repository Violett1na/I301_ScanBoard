/* test_force_release.c —— 强制套看门狗"释放逻辑"宿主单测(规范 8-1)
 * 构建/运行: sh tests/ls_force_wd/run.sh
 * 被测: ls_app_force_watchdog_poll() 与 ls_app_poll() 的时基刷新
 *   (定义于 App/ls_proto_device_app.c; 本用例链接该 TU, 外部依赖见
 *   stubs_ls_app_deps.c)。
 * 为什么单列: 纯函数 ls_force_wd_expired 已由 test_ls_force_wd.c 覆盖,
 *   但承载安全属性的两处不在其中——(a) ls_app_poll 收到整帧后刷新
 *   s_last_host_ms; (b) 看门狗据强制态与超时判定调 param_force_clear。
 *   删掉任一处, 纯函数用例仍会全绿而保护已消失, 故在此直接驱动。
 * 覆盖:
 *   1. 未强制 → 空转, 不调 param_force_clear;
 *   2. 已强制 + 陈旧时基 → 解除; 解除后再调为空转(已见释放态);
 *   3. 时基刷新来自整帧到达: 刷新后倒计时重新起算, 恰好 5000ms 不解除,
 *      5001ms 解除; 若 s_last_host_ms 刷新缺失, 用例 4 必红。 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ls_proto.h"             /* ls_pack: 造一整帧喂给 ls_app_poll */
#include "ls_proto_device_app.h"  /* 被测: ls_app_poll / 看门狗 */
#include "param.h"                /* PARAM_PROFILE_NONE 与 param_forced */
#include "stubs_ls_app_deps.h"

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

/* 交付一整帧给 ls_app_poll, 模拟上位机整帧到达并带上到达时刻时基。
 * 用 0x0103(重启): 其句柄只回调未注册的 reset_device, 不发帧、不触
 * 参数层, 适合只验"整帧到达 → 刷新看门狗时基"这一件事。 */
static uint8_t s_frame[64];   /* 打包后的整帧字节流(由桩的 peek 交付) */

static void feed_frame(uint32_t now_ms, uint16_t type_cmd)
{
    ls_packet_t pkt;    /* 待打包的帧体(载荷 1 字节占位) */
    uint16_t len = 0;   /* 打包后的帧长 */

    memset(&pkt, 0, sizeof(pkt));
    pkt.type     = (uint8_t)(type_cmd >> 8);
    pkt.cmd      = (uint8_t)(type_cmd & 0xFF);
    pkt.data_len = 1;
    pkt.data[0]  = 0xFF;
    pkt.pck_len  = LS_DATA_BASE_LEN + pkt.data_len;
    CHECK_EQ(ls_pack(&pkt, s_frame, &len), 0, "feed frame pack");

    stub_set_tick(now_ms);
    stub_set_rx(s_frame, len);
    ls_app_poll();
}

int main(void)
{
    /* 1. 未强制 → 空转: 时基再陈旧也不得解除 */
    stub_set_forced(PARAM_PROFILE_NONE);
    stub_set_tick(60000U);
    ls_app_force_watchdog_poll();
    CHECK_EQ(stub_force_clear_count(), 0, "not forced: no clear");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "not forced: unchanged");

    /* 2. 冷启动(s_last_host_ms 初值 0) + 陈旧时基 → 解除 */
    stub_set_forced(1U);
    stub_set_tick(6000U);   /* 距初值 0 已 6000ms, 超过 5000ms */
    ls_app_force_watchdog_poll();
    CHECK_EQ(stub_force_clear_count(), 1, "cold start stale: cleared");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "cold start: released");

    /* 2b. 解除后再调 → 空转: 函数已见到"未强制"的释放态 */
    stub_set_tick(70000U);
    ls_app_force_watchdog_poll();
    CHECK_EQ(stub_force_clear_count(), 1, "after release: no re-clear");

    /* 3. 整帧到达 → ls_app_poll 刷新时基并消费该帧 */
    feed_frame(100000U, LS_BASE_RESET_DEVICE);
    CHECK_EQ(stub_rx_consume_count(), 1, "frame consumed once");
    CHECK_EQ(stub_force_clear_count(), 1, "fresh frame: no clear");

    /* 3a. 距刷新恰 5000ms: 判据是严格大于, 仍保持强制 */
    stub_set_forced(2U);
    stub_set_tick(105000U);
    ls_app_force_watchdog_poll();
    CHECK_EQ(stub_force_clear_count(), 1, "exactly 5000ms: kept");
    CHECK_EQ(param_forced(), 2, "exactly 5000ms: still forced");

    /* 3b. 再过 1ms: 解除并回到未强制 */
    stub_set_tick(105001U);
    ls_app_force_watchdog_poll();
    CHECK_EQ(stub_force_clear_count(), 2, "5001ms: released");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "5001ms: released state");

    /* 4. 再收一整帧 → 时基重新起算。若 ls_app_poll 的刷新缺失,
     *    s_last_host_ms 仍停在 100000, 则下面 304000 会被误判超时 */
    feed_frame(300000U, LS_BASE_RESET_DEVICE);
    stub_set_forced(1U);
    stub_set_tick(304000U);
    ls_app_force_watchdog_poll();
    CHECK_EQ(stub_force_clear_count(), 2, "refresh resets countdown");
    CHECK_EQ(param_forced(), 1, "refresh: still forced");

    /* 5. 全程不得走到未建模的桩(第 2 节), 否则上面的断言失去意义 */
    CHECK_EQ(stub_offpath_calls(), 0, "no off-path stub called");

    printf("ls_force_release: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
