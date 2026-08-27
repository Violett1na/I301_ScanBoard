# 过流 PWM 信令（4053 切 FB 出口 + 硬件定时器方波）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 过流时把对应轴的 FB 出口（JB3/JB4）从模拟直通切到常开的 1kHz/50%/3V3 硬件 PWM 方波，向总控板报过流切激光；按轴独立、1s 防抖释放。

**Architecture:** 纯状态机核心放 `ocd_sig.h`（`static inline`，宿主 gcc 可测，同 `ad_da_deglitch.h` 先例）；固件胶水 `ocd_sig.c` 负责通路让路（停 DAC1_CH2/DAC4_CH2/OPAMP5）、PA5/PA8 复用推挽、TIM2/TIM1 常开 PWM、算法槽最外层包装（检测 + 切 PB3/PB4，不碰 `out[]`）。

**Tech Stack:** STM32G474 / STM32G4 HAL / EIDE+AC6（level-2）/ 宿主单测 WSL gcc（同 `tests/ad_da_deglitch` 先例）。

**Spec:** `docs/superpowers/specs/2026-08-27-ocd-pwm-signaling-design.md`

## Global Constraints

- **全程简体中文**（AGENTS.md）：注释、提交说明、与用户的交流一律中文。
- **未经用户明确授权，不得编译/烧录固件**（AGENTS.md）。宿主单元测试（WSL gcc）按既有先例执行，不在此限。
- 固件构建链 = VS Code + EIDE（AC6），优化 `level-2`（eide.yml 现状，勿动）；本特性不改管线热点代码。
- 当前分支 `Debug`；提交信息用中文，风格与既有一致（"新增/修复: 摘要"）。
- 头文件保护用 `__OCD_SIG_H__` 式；连续宏定义、连续赋值纵向对齐（AGENTS.md 风格规则）。
- 修改完代码后做一次简单总结（AGENTS.md）。
- peg 判据 ==0/4095；将来 ADC 量程修复后与 ocd 一处同步（spec §6）。

## File Structure

| 动作 | 文件 | 职责 |
|---|---|---|
| 新建 | `I301_code/Core/Inc/ocd_sig.h` | 参数宏 + 按轴纯状态机（宿主可测）+ 固件 API 声明 |
| 新建 | `I301_code/Core/Src/ocd_sig.c` | 固件胶水：让路、AF、TIM2/TIM1 PWM、算法槽包装、观测接口 |
| 新建 | `tests/ocd_sig/test_ocd_sig.c` | 宿主单元测试（纯核心） |
| 修改 | `I301_code/Core/Src/main.c:32,119` | `#include "ocd_sig.h"`；`ocd_init()` 后调 `ocd_sig_init()` |
| 修改 | `I301_code/MDK-ARM/.eide/eide.yml:40` | Core 文件清单加 `../Core/Src/ocd_sig.c` |

## 已核准的硬件事实（计划编写时逐一读码确认，2026-08-27）

- 通道映射（`task.c:96-100`）：`in[1]`=ix(ADC2/PA0)、`in[3]`=iy(ADC5/PA9)；PA5=DAC1_CH2 直出（DA_FBX）、PA8=DAC4_CH2+OPAMP5 跟随（DA_FBY）。
- 4053 选择脚：PB3=CH_FBX、PB4=CH_FBY（`main.h:119-122`），`gpio.c` 推挽+下拉、默认低 = 模拟直通。
- `htim2` 已存在（`tim.c:26`，`MX_TIM2_Init` PSC=170-1/ARR=99/TRGO=update，从未启动；其 `HAL_TIM_Base_MspInit` 使能了 TIM2_IRQn，需在初始化中禁掉）。
- TIM1 全工程未用；`task.c:744-756` 有 `htim6` 手工建句柄先例。工程无强定义 `HAL_TIM_MspInit`/`HAL_TIM_PWM_MspInit`（弱回调空实现，手工建句柄安全）。**TIM1 是 16 位定时器（ARR≤65535）**：1kHz 取 PSC=170−1、ARR=1000−1、CCR=500（170MHz/170/1000），TIM2 同配以统一常数；**勿用** ARR=170000−1 方案（仅 32 位定时器可用，HAL `IS_TIM_PERIOD` 对 16 位实例断言 ≤0xFFFF）。
- 本 HAL 无 `__HAL_TIM_GenerateEvent` 宏，只有 `HAL_TIM_GenerateEvent()` 函数（已核）；`HAL_TIM_PWM_Init` 结尾置全部通道态 READY（已核，`HAL_TIM_PWM_Start` 的通道态检查可通过）；`HAL_TIM_PWM_Start` 对 break 实例（TIM1）自动置 MOE（已核）。
- `hdac1/hdac4`（dac.h）、`hopamp5`（opamp.h）均导出；`HAL_DAC_Stop(hdac, ch)`、`HAL_OPAMP_Stop(hopamp)` 签名已核。
- `GPIO_AF1_TIM2`（gpio_ex.h:61）、`GPIO_AF6_TIM1`（gpio_ex.h:157）宏存在；PA5→AF1/TIM2_CH1、PA8→AF6/TIM1_CH1 已获两个网络来源佐证，**仍须按 Task 0 对照数据手册终审**。
- 算法规约：`ad_da_process_fn_t` 与 `AD_DA_CH_NUM`（`task.h:84-94`）；ocd.c 的包装手法与 `HAL_GetTick` 计时惯例（`ocd.c`）。

---

## Task 0: AF 号终审（硬件门，先于一切编码）

**Files:** 无（只读验证）

**背景**：计划中 `PA5 → GPIO_AF1_TIM2`、`PA8 → GPIO_AF6_TIM1`。若终审不符，Task 2 的 `ocd_sig_gpio_af_init()` 必须改用正确 AF 号，并在提交说明中记录。

- [ ] **Step 1: 查 STM32G474 数据手册复用功能表**

首选权威源：ST 官网数据手册（DS12994，stm32g474re.pdf）的 "Alternate function mapping" 表；本机无存档（`doc/` 只有原理图与器件规格书）。若网络受限取不到，请用户确认（用户手上有数据手册或可下载）。

核对两行：
- PA5 行：`TIM2_CH1` 对应的 AFSEL 值，期望 **AF1**
- PA8 行：`TIM1_CH1` 对应的 AFSEL 值，期望 **AF6**

- [ ] **Step 2: 记录结论**

若与期望一致：继续。若不一致：记下正确值，在 Task 2 Step 2 的 `ocd_sig_gpio_af_init()` 中采用，并在该任务提交说明中注明"AF 号经数据手册终审为 AFx/AFy"。

---

## Task 1: 纯状态机核心 `ocd_sig.h` + 宿主单测（TDD）

**Files:**
- Create: `I301_code/Core/Inc/ocd_sig.h`
- Test: `tests/ocd_sig/test_ocd_sig.c`

**Interfaces:**
- Consumes: 无（纯单元，只依赖 `<stdint.h>`）
- Produces: `ocd_sig_axis_t`（struct：`trips/t_clean/run/on/clean_armed`）、`ocd_sig_axis_init(ocd_sig_axis_t*)`、`ocd_sig_feed(ocd_sig_axis_t*, uint16_t sample, uint32_t now_ms) → uint8_t 事件`、宏 `OCD_SIG_EV_NONE/TRIP/RELEASE`、`OCD_SIG_TRIP_RUN=8U`、`OCD_SIG_RELEASE_MS=1000U`、`OCD_SIG_IS_PEG(s)`。Task 2 直接 `#include "ocd_sig.h"` 使用。

- [ ] **Step 1: 写测试骨架 + 跳闸组测试（红）**

创建 `tests/ocd_sig/test_ocd_sig.c`：

```c
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

int main(void)
{
    test_trip_after_run_gt8();
    test_peg_boundary_values();
    test_run_reset_by_non_peg();

    printf("ocd_sig 宿主单测: %d 通过, %d 失败\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
```

- [ ] **Step 2: 运行, 确认红（头文件不存在）**

```
wsl -- bash -c 'cd /mnt/d/Desktop/I301/I301_code-ad-da/tests/ocd_sig && gcc -std=c99 -Wall -Wextra -O2 -I ../../I301_code/Core/Inc test_ocd_sig.c -o test_ocd_sig && ./test_ocd_sig'
```

Expected: 编译失败，`fatal error: ocd_sig.h: No such file or directory`

- [ ] **Step 3: 写 `ocd_sig.h` v1（跳闸逻辑，释放分支留空）**

创建 `I301_code/Core/Inc/ocd_sig.h`：

```c
#ifndef __OCD_SIG_H__
#define __OCD_SIG_H__

/* ------------------------------------------------------------------
 * 过流 PWM 信令 —— 纯状态机单元(无副作用依赖, 宿主可测, 测试见 tests/ocd_sig)
 * spec: docs/superpowers/specs/2026-08-27-ocd-pwm-signaling-design.md
 *
 * 职责: 按轴独立检测过流(连续 peg), 给出跳闸/释放事件; 固件层(ocd_sig.c)
 *   据此切 4053 选择脚, 把该轴 FB 出口从模拟直通切到常开 PWM(1kHz/50%/3V3)
 *   报总控板切激光。本单元不做波形、不碰输出缓冲。
 *
 * 判据(同 ocd.c 语义, 按轴独立):
 *   peg = 样本==0 或 ==4095(撞量程轨);
 *   SIG_OFF: 连续 peg 计数 > OCD_SIG_TRIP_RUN 跳闸, 非 peg 清零;
 *   SIG_ON:  peg 清零释放计时, 无 peg 稳定 OCD_SIG_RELEASE_MS 释放。
 * ------------------------------------------------------------------ */

#include <stdint.h>

/* ---- 参数表(编译期宏, 集中管理, 依据见 spec §6) ---- */
#define OCD_SIG_ENABLE      1U            /* 总开关: 0 = 编译期整体摘除(空实现回退) */
#define OCD_SIG_TRIP_RUN    8U            /* 跳闸阈值: 连续 peg >8(即第 9 个)跳闸 */
#define OCD_SIG_RELEASE_MS  1000U         /* 防抖释放窗: 无 peg 稳定满 1s 回模拟直通 */
#define OCD_SIG_PSC         (170U - 1U)   /* 170MHz/170 = 1MHz 计数时钟(两路同配) */
#define OCD_SIG_ARR         (1000U - 1U)  /* 1MHz/1000 = 1kHz; TIM1 为 16 位, 此为其可容纳方案 */
#define OCD_SIG_CCR         500U          /* 500/1000 = 50% 占空 */
#define OCD_SIG_FORCE       0U            /* 诊断: 1 = 强置两轴信令开(上板验链), 用完回 0 */

/* ---- peg 判据(复用 ocd 语义: 撞量程轨) ---- */
#define OCD_SIG_IS_PEG(s)   (((s) == 0U) || ((s) == 4095U))

/* ---- ocd_sig_feed 返回事件 ---- */
#define OCD_SIG_EV_NONE     0U        /* 无状态迁移 */
#define OCD_SIG_EV_TRIP     1U        /* OFF->ON: 应拉高本轴 4053 选择脚 */
#define OCD_SIG_EV_RELEASE  2U        /* ON->OFF: 应拉低本轴 4053 选择脚 */

/* ---- 按轴独立状态机 ---- */
typedef struct
{
    uint32_t trips;       /* 累计跳闸次数 */
    uint32_t t_clean;     /* 无 peg 稳定窗起点 tick(ms) */
    uint16_t run;         /* 当前连续 peg 长度, ISR 独享 */
    uint8_t  on;          /* 0=SIG_OFF(模拟直通) 1=SIG_ON(PWM 信令) */
    uint8_t  clean_armed; /* 释放计时激活标志, ISR 独享 */
} ocd_sig_axis_t;

static inline void ocd_sig_axis_init(ocd_sig_axis_t *ax)
{
    ax->trips       = 0U;
    ax->t_clean     = 0U;
    ax->run         = 0U;
    ax->on          = 0U;
    ax->clean_armed = 0U;
}

/* 喂入一个样本, 返回事件。
 * now_ms = HAL_GetTick()(ISR 内读, 误差 ≤1ms, 对 1s 窗口无影响)。 */
static inline uint8_t ocd_sig_feed(ocd_sig_axis_t *ax, uint16_t sample, uint32_t now_ms)
{
    uint8_t peg = OCD_SIG_IS_PEG(sample) ? 1U : 0U;
    uint8_t ev  = OCD_SIG_EV_NONE;
    (void)now_ms;   /* v1 仅跳闸逻辑; 释放计时在 v2 引入 */

    if (ax->on == 0U)
    {
        /* SIG_OFF: 连续 peg 计数, 超阈跳闸 */
        if (peg != 0U)
        {
            ax->run++;
            if (ax->run > OCD_SIG_TRIP_RUN)
            {
                ax->on   = 1U;
                ax->run  = 0U;
                ax->trips++;
                ev = OCD_SIG_EV_TRIP;
            }
        }
        else
        {
            ax->run = 0U;   /* 非 peg 清零连续计数, 滤孤立毛刺 */
        }
    }
    return ev;
}

/* ---- 固件 API(实现见 ocd_sig.c; 宿主单测只用上面的纯单元) ---- */
void     ocd_sig_init(void);        /* ocd_init() 之后调用一次 */
uint8_t  ocd_sig_state(void);       /* bit0 = X 轴 SIG_ON, bit1 = Y 轴 SIG_ON */
uint32_t ocd_sig_trip_count(void);  /* 累计跳闸次数(两轴合计, 观测用) */

#endif /* __OCD_SIG_H__ */
```

- [ ] **Step 4: 运行, 确认 A 组绿**

同 Step 2 命令。Expected: `ocd_sig 宿主单测: N 通过, 0 失败`（N 为 A 组断言数），退出码 0。

- [ ] **Step 5: 追加 B 组测试（释放与清零重计），确认为红**

在 `main()` 之前追加：

```c
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
```

`main()` 中追加调用：

```c
    test_release_after_1s_clean();
    test_release_reset_by_peg();
    test_no_release_while_pegged();
```

运行（同 Step 2 命令）。Expected: 失败 —— `test_release_after_1s_clean` 报"无 peg 满 1s 应释放"（v1 无释放逻辑，跳闸后恒不返回事件且恒不释放）。

- [ ] **Step 6: 在 `ocd_sig_feed` 中实现释放分支，确认绿**

把 `ocd_sig.h` 中 `ocd_sig_feed` 的函数体替换为：

```c
static inline uint8_t ocd_sig_feed(ocd_sig_axis_t *ax, uint16_t sample, uint32_t now_ms)
{
    uint8_t peg = OCD_SIG_IS_PEG(sample) ? 1U : 0U;
    uint8_t ev  = OCD_SIG_EV_NONE;

    if (ax->on == 0U)
    {
        /* SIG_OFF: 连续 peg 计数, 超阈跳闸 */
        if (peg != 0U)
        {
            ax->run++;
            if (ax->run > OCD_SIG_TRIP_RUN)
            {
                ax->on          = 1U;
                ax->run         = 0U;
                ax->clean_armed = 0U;
                ax->trips++;
                ev = OCD_SIG_EV_TRIP;
            }
        }
        else
        {
            ax->run = 0U;   /* 非 peg 清零连续计数, 滤孤立毛刺 */
        }
    }
    else
    {
        /* SIG_ON: 防抖释放 —— peg 清零重计, 无 peg 稳定满窗释放 */
        if (peg != 0U)
        {
            ax->clean_armed = 0U;
        }
        else if (ax->clean_armed == 0U)
        {
            ax->clean_armed = 1U;
            ax->t_clean     = now_ms;
        }
        else if ((now_ms - ax->t_clean) >= OCD_SIG_RELEASE_MS)
        {
            ax->on          = 0U;
            ax->clean_armed = 0U;
            ev = OCD_SIG_EV_RELEASE;
        }
    }
    return ev;
}
```

运行（同 Step 2 命令）。Expected: A+B 组全绿，0 失败。

- [ ] **Step 7: 追加 C 组测试（双轴独立 / 释放后再触发），确认绿**

在 `main()` 之前追加：

```c
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
```

`main()` 中追加调用：

```c
    test_axes_independent();
    test_retrip_after_release();
```

运行（同 Step 2 命令）。Expected: 全绿，0 失败。

- [ ] **Step 8: 提交**

```bash
git add I301_code/Core/Inc/ocd_sig.h tests/ocd_sig/test_ocd_sig.c
git commit -m "新增过流信令纯状态机核心 ocd_sig.h 与宿主单测: 按轴跳闸(>8连续peg)/1s防抖释放/双轴独立, WSL gcc 全过"
```

---

## Task 2: 固件模块 `ocd_sig.c` + 接入 main.c 与 EIDE 工程

**Files:**
- Create: `I301_code/Core/Src/ocd_sig.c`
- Modify: `I301_code/Core/Src/main.c`（include 区 + `ocd_init()` 调用后）
- Modify: `I301_code/MDK-ARM/.eide/eide.yml`（Core 文件清单）

**Interfaces:**
- Consumes: Task 1 的 `ocd_sig.h`；`ad_da_process_fn`/`AD_DA_CH_NUM`（task.h）；`htim2`（tim.h）；`hdac1/hdac4`（dac.h）；`hopamp5`（opamp.h）；`CH_FBX_Pin/CH_FBX_GPIO_Port/CH_FBY_Pin/CH_FBY_GPIO_Port`（main.h）；`GPIO_AF1_TIM2/GPIO_AF6_TIM1`（stm32g4xx_hal_gpio_ex.h，AF 号以 Task 0 终审为准）。
- Produces: `ocd_sig_init()`（main.c 调用）、`ocd_sig_state()`、`ocd_sig_trip_count()`（后续观测/日志用）。

- [ ] **Step 1: 创建 `ocd_sig.c`**

```c
/* ocd_sig.c — 过流 PWM 信令固件模块
 * spec: docs/superpowers/specs/2026-08-27-ocd-pwm-signaling-design.md
 *
 * 职责:
 *   1. 通路让路: 停 DAC1_CH2/DAC4_CH2/OPAMP5, 释放 PA5/PA8;
 *   2. PWM 常开: TIM2_CH1→PA5(X 轴)、TIM1_CH1→PA8(Y 轴),
 *      1kHz/50%/3V3, 上电即跑、永不启停;
 *   3. 算法槽最外层包装(链: linear→ocd→ocd_sig): 按轴独立检测
 *      (纯单元见 ocd_sig.h), 跳闸/释放只写 4053 选择脚 PB3/PB4,
 *      不填样本、不碰 out[]。
 * Fail-safe: PB3/PB4 在 gpio.c 下拉默认低 = 模拟直通; MCU 死机
 *   信令丢失但反馈链路保持现状(与 ocd 同一固有边界, spec §8)。
 * 时序: 计时用 HAL_GetTick()(ISR 内读, 误差 ≤1ms), 同 ocd 惯例。 */

#include "ocd_sig.h"
#include "task.h"     /* ad_da_process_fn_t / AD_DA_CH_NUM / ad_da_process_fn */

#if OCD_SIG_ENABLE

/* ---- 模块内部状态 ---- */
static ad_da_process_fn_t s_inner;      /* 被包装的内层算法(= ocd 包装层) */
static ocd_sig_axis_t     s_axis_x;     /* X 轴状态机, 检测源 ix = in[1] */
static ocd_sig_axis_t     s_axis_y;     /* Y 轴状态机, 检测源 iy = in[3] */

/* ---- 通路让路: PA5 现 = DAC1_CH2 直出, PA8 现 = DAC4_CH2 + OPAMP5 跟随;
 *      IN 通道(DAC1_CH1/DAC4_CH1)与 OPAMP4 不动 ---- */
static void ocd_sig_route_takeover(void)
{
    if (HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    if (HAL_DAC_Stop(&hdac4, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Stop(&hopamp5)            != HAL_OK) Error_Handler();
}

/* ---- PA5/PA8 改定时器复用推挽 ----
 * AF 号对照 STM32G474 数据手册复用功能表确认(见 Task 0 终审记录):
 *   PA5: AF1 = TIM2_CH1; PA8: AF6 = TIM1_CH1。
 * 4053 X1/Y1 就挂在这两个网(经 R6/R8 出板到 JB3/JB4), 零改板。 */
static void ocd_sig_gpio_af_init(void)
{
    GPIO_InitTypeDef gi = {0};

    gi.Pin       = GPIO_PIN_5;          /* X 轴 PWM: PA5 → TIM2_CH1 */
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_NOPULL;
    gi.Speed     = GPIO_SPEED_FREQ_LOW; /* 1kHz 方波, 低摆率利于 EMI */
    gi.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &gi);

    gi.Pin       = GPIO_PIN_8;          /* Y 轴 PWM: PA8 → TIM1_CH1 */
    gi.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(GPIOA, &gi);
}

/* ---- X 轴 PWM: 复用 htim2(.ioc 已建, 从未启动) ----
 * 原配 PSC=170-1/ARR=99 为未启用的旧用途, 此处改 1kHz PWM 时基:
 * PSC=170-1(170MHz/170=1MHz)、ARR=1000-1(1MHz/1000=1kHz)、CCR=500(50%)。
 * 与 TIM1 同配: TIM1 为 16 位定时器(ARR≤65535), 此为其可容纳的 1kHz 方案。
 * 不开更新中断; NVIC 禁 TIM2_IRQn —— 规避 task.c TIM2 中断死代码
 * 旧账(回调直写 DHR 与 TX DMA 竞争, README 警示)。
 * 注: HAL_TIM_PWM_Init 见 State=READY(MX_TIM2_Init 已置)不再回调
 * MspInit, 不会重新使能 TIM2_IRQn; 原 TRGO=update 主模式配置不受影响。 */
static void ocd_sig_pwm_tim2_init(void)
{
    TIM_OC_InitTypeDef oc = {0};

    htim2.Init.Prescaler         = OCD_SIG_PSC;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = OCD_SIG_ARR;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = OCD_SIG_CCR;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

    if (HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE) != HAL_OK) Error_Handler(); /* PSC 影子装载 */
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

    __HAL_TIM_DISABLE_IT(&htim2, TIM_IT_UPDATE);              /* 纯输出, 不需要中断 */
    HAL_NVIC_DisableIRQ(TIM2_IRQn);                           /* 见函数头注释 */

    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
}

/* ---- Y 轴 PWM: TIM1 全工程未用, 按 task.c htim6 先例手工建句柄 ----
 * TIM1 为高级定时器(带刹车单元), HAL_TIM_PWM_Start 内部处理 MOE。
 * State=RESET 时 HAL_TIM_PWM_Init 回调的是弱定义 HAL_TIM_PWM_MspInit
 * (工程无强定义, 空操作), 时钟已在下面手动使能。
 * ⚠️ TIM1 是 16 位定时器(ARR≤65535): 时基与 TIM2 完全同配
 * (PSC=170-1/ARR=1000-1/CCR=500), 不可用 ARR=170000-1 大周期方案。 */
static TIM_HandleTypeDef s_htim1;       /* .ioc 未登记, 模块私有 */

static void ocd_sig_pwm_tim1_init(void)
{
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_TIM1_CLK_ENABLE();
    s_htim1.Instance               = TIM1;
    s_htim1.Init.Prescaler         = OCD_SIG_PSC;
    s_htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim1.Init.Period            = OCD_SIG_ARR;
    s_htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim1.Init.RepetitionCounter = 0U;
    s_htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&s_htim1) != HAL_OK) Error_Handler();

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = OCD_SIG_CCR;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_htim1, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

    if (HAL_TIM_GenerateEvent(&s_htim1, TIM_EVENTSOURCE_UPDATE) != HAL_OK) Error_Handler(); /* PSC 影子装载 */
    __HAL_TIM_SET_COUNTER(&s_htim1, 0U);
    __HAL_TIM_CLEAR_FLAG(&s_htim1, TIM_FLAG_UPDATE);

    if (HAL_TIM_PWM_Start(&s_htim1, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
}

/* ---- 算法槽最外层包装: 逐样本检测 + 只切 4053 选择脚,
 *      out[] 无条件委托内层(本模块不改任何通道输出) ---- */
static void ocd_sig_process(const uint16_t *in[AD_DA_CH_NUM],
                            uint16_t       *out[AD_DA_CH_NUM],
                            uint16_t        n)
{
    uint32_t        now = HAL_GetTick();
    const uint16_t *pix = in[1];   /* ix */
    const uint16_t *piy = in[3];   /* iy */

    for (uint16_t i = 0U; i < n; i++)
    {
        uint8_t evx = ocd_sig_feed(&s_axis_x, pix[i], now);
        uint8_t evy = ocd_sig_feed(&s_axis_y, piy[i], now);

        if (evx == OCD_SIG_EV_TRIP)
        {
            HAL_GPIO_WritePin(CH_FBX_GPIO_Port, CH_FBX_Pin, GPIO_PIN_SET);
        }
        else if (evx == OCD_SIG_EV_RELEASE)
        {
            HAL_GPIO_WritePin(CH_FBX_GPIO_Port, CH_FBX_Pin, GPIO_PIN_RESET);
        }

        if (evy == OCD_SIG_EV_TRIP)
        {
            HAL_GPIO_WritePin(CH_FBY_GPIO_Port, CH_FBY_Pin, GPIO_PIN_SET);
        }
        else if (evy == OCD_SIG_EV_RELEASE)
        {
            HAL_GPIO_WritePin(CH_FBY_GPIO_Port, CH_FBY_Pin, GPIO_PIN_RESET);
        }
    }

#if OCD_SIG_FORCE
    /* 诊断: 强置两轴信令开(每拍覆写, 压过状态机的一切切换;
       只影响电气链路验证, 不改动状态机自身计数) */
    HAL_GPIO_WritePin(CH_FBX_GPIO_Port, CH_FBX_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CH_FBY_GPIO_Port, CH_FBY_Pin, GPIO_PIN_SET);
#endif

    s_inner(in, out, n);   /* 透传: 检测与输出完全解耦 */
}

/* ---- 挂载: main.c 中 ocd_init() 之后调用一次 ----
 * 顺序: 轴状态 → 通路让路 → 引脚复用 → 起 PWM → 包装算法槽(最后赋值)。
 * 所有硬件改动都在本函数内: OCD_SIG_ENABLE=0 时无任何残留。 */
void ocd_sig_init(void)
{
    ocd_sig_axis_init(&s_axis_x);
    ocd_sig_axis_init(&s_axis_y);

    ocd_sig_route_takeover();
    ocd_sig_gpio_af_init();
    ocd_sig_pwm_tim2_init();
    ocd_sig_pwm_tim1_init();

    s_inner          = ad_da_process_fn;
    ad_da_process_fn = ocd_sig_process;   /* 最后赋值: 此后管线经包装层 */
}

/* ---- 观测接口(主循环/日志用; 单字节读在 Cortex-M 上原子) ---- */
uint8_t ocd_sig_state(void)
{
    return (uint8_t)(s_axis_x.on | (uint8_t)(s_axis_y.on << 1));
}

uint32_t ocd_sig_trip_count(void)
{
    return s_axis_x.trips + s_axis_y.trips;
}

#else /* !OCD_SIG_ENABLE —— 编译期整体摘除, 空实现保调用点免改 */

void ocd_sig_init(void) {}
uint8_t ocd_sig_state(void) { return 0U; }
uint32_t ocd_sig_trip_count(void) { return 0U; }

#endif /* OCD_SIG_ENABLE */
```

注意：若 Task 0 终审 AF 号与本文件不一致，只改 `ocd_sig_gpio_af_init()` 中两处 `gi.Alternate`。

- [ ] **Step 2: 接入 main.c**

`I301_code/Core/Src/main.c` 头部 include 区（现有第 31-33 行为 `task.h`、`ocd.h`、`usb_device.h`），在 `#include "ocd.h"` 之后插入：

```c
#include "ocd_sig.h"
```

初始化段（现有第 118-119 行）：

```c
  AD_DA_Init();
  ocd_init();   /* 软件过流检测: 包装算法槽, 须在 AD_DA_Init 之后 */
```

在 `ocd_init();` 之后插入一行，成为：

```c
  AD_DA_Init();
  ocd_init();   /* 软件过流检测: 包装算法槽, 须在 AD_DA_Init 之后 */
  ocd_sig_init();   /* 过流 PWM 信令: 最外层包装, 须在 ocd_init 之后 */
```

- [ ] **Step 3: 登记进 EIDE 工程**

`I301_code/MDK-ARM/.eide/eide.yml` 的 Core 文件清单（User→Core 节，`- path: ../Core/Src/ocd.c` 之后）插入同级一行：

```yaml
                - path: ../Core/Src/ocd_sig.c
```

- [ ] **Step 4: 静态自查（不编译——未经授权）**

逐条核对（全部应成立，任一不成立则回改）：
- `ocd_sig_process` 中 `in[1]`/`in[3]` 与 `task.c:96-100` 通道注释一致；
- `CH_FBX_Pin/CH_FBY_Pin` 宏名与 `main.h:119-122` 逐字一致；
- `HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2)`、`HAL_DAC_Stop(&hdac4, DAC_CHANNEL_2)`、`HAL_OPAMP_Stop(&hopamp5)` 三处让路齐全，且未动 DAC1_CH1/DAC4_CH1/OPAMP4；
- `ocd_sig_init()` 内最后一步才是 `ad_da_process_fn = ocd_sig_process;`；
- eide.yml 新行缩进与相邻行一致（16 空格 + `- path:`）；
- 用 Grep 确认 `OCD_SIG_TRIP_RUN`、`OCD_SIG_RELEASE_MS`、`OCD_SIG_PSC`、`OCD_SIG_ARR`、`OCD_SIG_CCR`、`OCD_SIG_EV_*` 在 .h/.c/测试三处名称完全一致；
- 确认两路定时器时基数值：`OCD_SIG_ARR`=999 ≤ 0xFFFF（TIM1 为 16 位定时器），170MHz/(170×1000)=1kHz、500/1000=50%；
- 用 Grep 确认工程无 `HAL_TIM_MspInit`/`HAL_TIM_PWM_MspInit` 强定义（`stm32g4xx_hal_msp.c` 与各 .c），保证 TIM1 手工句柄不触发意外回调。

- [ ] **Step 5: 提交**

```bash
git add I301_code/Core/Src/ocd_sig.c I301_code/Core/Src/main.c I301_code/MDK-ARM/.eide/eide.yml
git commit -m "新增过流PWM信令固件模块 ocd_sig.c: 停DAC1_CH2/DAC4_CH2/OPAMP5让路, TIM2_CH1->PA5/TIM1_CH1->PA8常开1kHz/50%方波, 算法槽最外层按轴切4053; 接入 main.c 与 eide 工程"
```

（固件编译验证需用户授权，见 Task 3。）

---

## Task 3: FORCE 诊断上板验证（用户操作，执行者引导）

**Files:**
- Modify（临时）: `I301_code/Core/Inc/ocd_sig.h`（`OCD_SIG_FORCE` 1U ↔ 0U，最终状态必须 0U）

**说明**：编译/烧录由用户在 EIDE（AC6, level-2）中执行；执行者负责改宏、给判据、收尾。本任务不产生提交（宏值终态 = 已提交的 0U），除非过程中发生代码修改。

- [ ] **Step 1: 置诊断宏**

`ocd_sig.h` 中 `OCD_SIG_FORCE` 改 `1U`。**不提交**。请用户构建并烧录（提醒既有约束：JP3 断开，见 2026-08-04 spec §3.2）。

- [ ] **Step 2: 示波器判据（用户执行，逐项过）**

1. JB3 pin4（FBX）：1kHz ±5%、占空 50% ±5%、摆幅 0V↔3V3 方波；
2. JB4 pin4（FBY）：同上；
3. MCU 侧 PB3、PB4 均为高电平；
4. PA5、PA8 直接测：同为 1kHz/50% 方波（确认 4053 前级正常，用于隔离 4053 故障）；
5. 断开一侧振镜/4053 供电对比不影响另一轴（可选）。

排障速查：
- PA5/PA8 无波形 → 查 Task 0 AF 号、`HAL_TIM_PWM_Start` 返回、TIM 时钟；
- PA5/PA8 有、JB 出口无 → 查 4053 供电与 PB3/PB4 电平、R6/R8；
- 频率不对 → 查 `OCD_SIG_PSC`（应 170−1）、`OCD_SIG_ARR`（应 1000−1）、定时时钟 170MHz 假设；TIM1 侧另查 ARR 是否误用 >0xFFFF 的值。

- [ ] **Step 3: 恢复常态**

`OCD_SIG_FORCE` 改回 `0U`；`git status` 应干净（宏值回到已提交状态）。请用户重新构建并烧录常态固件。

- [ ] **Step 4: 收尾**

向用户做修改总结（AGENTS.md 要求）：本特性三个提交、改动文件清单、FORCE 验证结果。真过流联调为后续独立事项（spec §9 第 2 步），不在本计划内。

---

## 自检记录（计划编写者，2026-08-27）

- **Spec 覆盖**：§1 背景→目标与 Task 3 收尾；§2 硬件事实→"已核准的硬件事实"节与 Task 0；§3 与 ocd 关系→Task 2（ocd 不改，包装在外层）；§4 状态机→Task 1（全部语义有测试：触发/释放/清零重计/双轴/再触发/边界/持续钉轨）；§5 PWM 生成→Task 2 Step 1（让路、AF、TIM2/TIM1、禁中断）；§6 参数表→ocd_sig.h 宏（逐一对应）；§7 接口与挂载→Task 1 Produces + Task 2；§8 fail-safe→ocd_sig.c 文件头注释 + Task 3 判据；§9 测试/上板/回退→Task 1/Task 3/`OCD_SIG_ENABLE=0` 空实现；§10 deferred→不在本计划。
- **占位符扫描**：无 TBD/TODO；所有代码步骤给出完整代码；命令与预期输出完整。
- **类型一致性**：`ocd_sig_axis_t`、`ocd_sig_feed`、`OCD_SIG_EV_*`、`OCD_SIG_TRIP_RUN/RELEASE_MS/PSC/ARR/CCR/FORCE` 三个任务间名称与签名一致；`ocd_sig_init/state/trip_count` 声明（.h）与实现（.c）一致。
- **计划阶段修正（已同步 spec，commit a6b54fb）**：① TIM1 为 16 位定时器，初稿 ARR=170000−1 放不下，两路统一改 PSC=170−1/ARR=1000−1/CCR=500；② 本 HAL 无 `__HAL_TIM_GenerateEvent` 宏，改用 `HAL_TIM_GenerateEvent()` 函数并检查返回值。
