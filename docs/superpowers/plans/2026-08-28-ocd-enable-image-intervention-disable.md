# OCD_ENABLE 摘除过流图像干预 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 过流时图像输出零改变、仅发 4053 方波信令——给 `ocd.c` 加 `OCD_ENABLE` 编译开关（默认 0 摘除干预），保留 `ocd_sig` 信令链路不动。

**Architecture:** 完全仿照 `OCD_SIG_ENABLE` 先例：宏定义放 `ocd.h` 参数区；`ocd.c` 实现整体包进 `#if OCD_ENABLE`，`#else` 分支提供 `ocd_init/ocd_state/ocd_trip_count` 空实现，调用点（`main.c`）免改；`eide.yml` 文件列表不动（空实现照常编译，规避活动 EIDE 实例回写风险）。算法槽链由 `linear→ocd→ocd_sig` 变为 `linear→ocd_sig`。

**Tech Stack:** STM32G474 固件（C99/armclang -O2，EIDE 构建）；宿主回归测试走 WSL gcc（仅 `tests/ocd_sig`，纯状态机）。

**Spec:** `docs/superpowers/specs/2026-08-28-ocd-image-intervention-disable-design.md`

## Global Constraints

- JP3 必须断开；两源叠加禁令（spec §3.2）。
- 未经授权不编译烧录固件（宿主单测 WSL gcc 除外）；构建/烧录由用户执行。
- 全程中文注释；改码后总结。
- 本轮单变量：`ocd_sig` 判据/阈值/释放窗一律不动；`eide.yml` 源文件列表不动。
- 构建前确认 `eide.yml` 含 `../Core/Src/ocd_sig.c` 与 `../Core/Src/ocd.c`、优化 level-2（活动 EIDE 实例有回写前科）。

---

### Task 1: OCD_ENABLE 宏 + ocd.c 编译期摘除 + main.c 注释

**Files:**
- Modify: `I301_code/Core/Inc/ocd.h`（参数区，约 24 行处）
- Modify: `I301_code/Core/Src/ocd.c`（头部注释、`#include` 之后、文件尾）
- Modify: `I301_code/Core/Src/main.c`（约 120 行 `ocd_init()` 挂载处）

**Interfaces:**
- 保持不变（本任务不改任何接口）：`void ocd_init(void);` `uint8_t ocd_state(void);` `uint32_t ocd_trip_count(void);` —— 宏为 0 时三者为空实现/常量返回，`main.c` 调用点无需改动。
- `OCD_ENABLE` 语义：`0U`=编译期摘除图像干预（本轮提交态）；`1U`=恢复 200ms 强置安全值+1s 盲期。

- [ ] **Step 1: ocd.h 参数区加宏**

在 `ocd.h` 现有参数块（`#define OCD_TRIP_RUN 8U` 之前）插入：

```c
#define OCD_ENABLE     0U      /* 总开关: 0 = 编译期摘除图像干预(过流只发信令不动图像,
                                  2026-08-28 用户决策; 原 200ms 强置+1s 盲期行为见 #if 分支);
                                  1 = 恢复原行为(真过流联调对照时可切回) */
```

- [ ] **Step 2: ocd.c 实现包进 #if，尾部加 #else 空实现**

`ocd.c` 中 `#include "ocd.h"` 之后、`/* ---- 模块内部状态 ---- */` 之前插入一行：

```c
#if OCD_ENABLE
```

文件尾（`ocd_trip_count` 函数之后、`/* file end */` 之前）追加：

```c
#else /* !OCD_ENABLE —— 编译期整体摘除图像干预, 空实现保调用点免改
         (与 ocd_sig.c 尾部回退模式一致)。信令链路由 ocd_sig 独立承担,
         算法槽链 = linear → ocd_sig。2026-08-28 用户决策, 见
         docs/superpowers/specs/2026-08-28-ocd-image-intervention-disable-design.md */

void ocd_init(void) {}
uint8_t ocd_state(void) { return OCD_STATE_ARMED; }   /* 未干预: 报告"武装态"供观测 */
uint32_t ocd_trip_count(void) { return 0U; }

#endif /* OCD_ENABLE */
```

并把 `ocd.c` 文件头时序注释首行补一句（在"时间线: ARMED ──…"注释块内追加）：

```c
 * ⚠️ 本行为受 OCD_ENABLE 宏门控(见 ocd.h): 0 = 整体摘除(过流只发信令不动图像)。
```

- [ ] **Step 3: main.c 挂载处注释更新**

`main.c` 中：

```c
  ocd_init();   /* 软件过流检测: 包装算法槽, 须在 AD_DA_Init 之后 */
```

改为：

```c
  ocd_init();   /* 软件过流检测(命令级干预): 受 OCD_ENABLE 宏门控(现=0, 空实现,
                   过流只由 ocd_sig 发信令、图像零改变); 须在 AD_DA_Init 之后 */
```

- [ ] **Step 4: 宿主回归测试（防误伤）**

运行：

```bash
wsl -- bash -c 'cd /mnt/d/Desktop/I301/I301_code-ad-da/tests/ocd_sig && gcc -std=c99 -Wall -Wextra -O2 -I ../../I301_code/Core/Inc test_ocd_sig.c -o test_ocd_sig && ./test_ocd_sig'
```

预期：全部断言通过（`ocd_sig.h` 纯状态机不受本改动影响，此为回归保险）。

- [ ] **Step 5: 静态核对两个宏分支**

用 `grep -n "OCD_ENABLE" I301_code/Core/Inc/ocd.h I301_code/Core/Src/ocd.c I301_code/Core/Src/main.c` 核对：宏仅出现在 `ocd.h`（定义）与 `ocd.c`（`#if/#endif` 门控）；`#else` 分支三个空实现齐全；`main.c` 调用点未变。

- [ ] **Step 6: 提交**

```bash
git add I301_code/Core/Inc/ocd.h I301_code/Core/Src/ocd.c I301_code/Core/Src/main.c
git commit -m "新增OCD_ENABLE宏(=0): 过流图像干预编译期摘除——只发方波不动图像; ocd.c空实现回退模式同ocd_sig; 单变量(信令链不动), 真过流联调对照可置1恢复"
```

---

### Task 2: 固件构建与上板验证（用户执行，出结果后回报）

**前置检查（每次构建前）**：`eide.yml` 源列表含 `../Core/Src/ocd_sig.c` 与 `../Core/Src/ocd.c`，优化 `level-2`（活动 EIDE 实例有回写前科）；JP3 断开。

- [ ] **Step 1: 用户 EIDE 构建并烧录**（`OCD_ENABLE=0` 提交态，无需改码）。
- [ ] **Step 2: 启动判据**：串口时间戳数据流越过 255ms 持续输出不中断（冻结修复不回退）。
- [ ] **Step 3: 图像判据**：打圆/扫描出现折返冲轨（ix 读 0000/4095）时，图像**不再被强置中点**（对照旧行为的 200ms 回中抖动消失）。
- [ ] **Step 4: 信令判据**：真过流时出板方波出现，重点确认是否由"时有时无"转为**持续**（`ocd` 救援循环消失后 `ocd_sig` 应锁在跳闸态）。
- [ ] **Step 5: 结果回报**：三项判据结果 + 方波形态描述；若方波仍断续，转 spec §8 待办（启动门/释放语义）。
