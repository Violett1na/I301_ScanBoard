# I301 三套配置（下位机侧）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 I301 振镜 XY 板固件持久化三套码值配置，并对外提供整包读写、强制套控制、全量回读的协议接口。

**Architecture:** 协议层（`Proto/`，纯 C 零 HAL）先行并以宿主单测 TDD；`param` 层从「单套」扩为「三套 + 生效/强制索引」；`ls_proto_device_app.c` 只做协议字段到 `param` 接口的胶水映射。

**Tech Stack:** C99、STM32G474 固件（四层架构：App / Bsp / Port / Platform）、宿主单测走 MinGW gcc。

**上游 spec:** `docs/superpowers/specs/2026-09-15-i301-three-profiles-design.md`（版本 2026-09-15-B）

## Global Constraints

- **语言**：代码注释、日志、提交信息一律简体中文（项目规范 0-2）。
- **缩进**：4 空格，禁用 TAB（项目规范 1-2）。
- **命名族**：用户层小写 snake_case，函数名中缩写小写（`ls_ctrl_set_profile`）；宏全大写（`LS_CTRL_SET_PROFILE`）；类型小写 + `_t` 后缀（项目规范 3-8/3-9）。
- **分层**：协议层不得 include App 层 `param.h`（规范 13-3）；`param` 实体保持 `static` 私有，写方单点收敛（规范 13-1）。
- **大小端**：`radc[6]` 单字节不转换；`comp_x`/`comp_y`/`device_id`/`device_version` 为多字节，须经 `ls_swap_endian_16()` 转换（沿用现有 `LS_ENDIAN_ENABLE` / `LS_RX_ENDIAN_ENABLE` 开关）。
- **尺寸**：`flash_store_t` = 30 字节，必须 ≤ `BSP_FLASH_DATA_MAX`(32)；`param_flash_fit_check` 编译期校验必须保持通过。
- **测试命令**：`gcc` 位于 `/c/Qt/Tools/mingw1310_64/bin/gcc.exe`（不在 PATH，需全路径）。测试运行方式沿用既有先例：仓库根执行 `sh tests/<模块>/run.sh`。
- **⭐ 提交须授权**：项目规范 11-10 要求每次 git 提交的粒度与信息经用户批准。**每个 Task 末尾的 commit 步骤执行前必须先向用户出示提交信息并获批**，不得自行提交。
- **⭐ 编译/烧录须授权**：项目 AGENTS.md 规定 AI 不得自行进入编译/运行/部署；固件编译（Keil MDK）与烧录由用户执行（规范 8-6）。本计划的宿主单测（gcc）可自行运行。
- **⭐ 协议双仓镜像**：`Proto/` 下的 `ls_proto*.c/h` 在上位机仓库 `USB_I301_QT/scan_setting/usb_i301/Proto/` 另存一份。**本计划只改下位机副本**；上位机副本的同步在后续「上位机计划」中作为首个 Task 执行。

---

## 文件结构

| 文件 | 职责 | 本计划动作 |
|---|---|---|
| `I301_code/Proto/Inc/ls_proto_data.h` | 命令字枚举 + 协议结构体（两仓镜像） | 修改：加 4 个命令字 + 5 个结构体 |
| `I301_code/Proto/Inc/ls_proto_trans.h` | 发送层接口 + 发送回调集 | 修改：加 4 个函数声明 + 1 个回调类型 |
| `I301_code/Proto/Src/ls_proto_trans.c` | 发送层实现 | 修改：加 4 个函数实现 |
| `I301_code/Proto/Inc/ls_proto_receive.h` | 接收层接口 + 接收回调集 | 修改：加 4 个 handler 声明 + 3 个回调类型 |
| `I301_code/Proto/Src/ls_proto_receive.c` | 接收层实现 + `ls_handle` 分发 | 修改：加 4 个 handler + 4 个 case |
| `I301_code/App/param.h` | 参数类型与接口 | 修改：单套 → 三套模型 |
| `I301_code/App/param.c` | 参数实体（`static` 私有） | 修改：三套存储 + 生效/强制索引 + 应用逻辑 |
| `I301_code/App/ls_proto_device_app.c` | 协议 ↔ 应用胶水 | 修改：实现 4 个新回调 + 强制套看门狗 |
| `I301_code/App/ls_proto_device_app.h` | 胶水层接口 | 修改：声明看门狗函数 |
| `I301_code/Core/Src/main.c` | 主循环（用户代码区） | 修改：第 146 行后加看门狗调用 |
| `tests/ls_proto/` | 协议层宿主单测（新增） | 创建 |
| `tests/param/` | 参数层宿主单测（新增） | 创建 |

---

## Task 1: 协议层宿主单测脚手架 + 现有协议回归基线

先给**现有**协议立基线，保证后续改动不破坏既有行为。

**Files:**
- Create: `tests/ls_proto/run.sh`
- Create: `tests/ls_proto/test_ls_proto.c`

**Interfaces:**
- Consumes: `ls_pack` / `ls_unpack` / `ls_crc16` / `ls_swap_endian_16`（`I301_code/Proto/Src/ls_proto.c`，已存在）
- Produces: `tests/ls_proto/run.sh` 供后续 Task 复用；`CHECK_EQ` / `CHECK_TRUE` 宏在本文件内定义

- [ ] **Step 1: 写构建脚本**

创建 `tests/ls_proto/run.sh`：

```sh
#!/bin/sh
# run.sh —— ls_proto 协议层宿主单测构建与运行(规范 8-1: 宿主测试先行)
# 用法: 仓库根目录执行  sh tests/ls_proto/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

GCC="${GCC:-/c/Qt/Tools/mingw1310_64/bin/gcc.exe}"

"$GCC" -std=c99 -Wall -Wextra -O2 \
    -I "$ROOT/I301_code/Proto/Inc" \
    -I "$ROOT/I301_code/Proto/Src" \
    "$ROOT/tests/ls_proto/test_ls_proto.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto_trans.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto_receive.c" \
    -o "$ROOT/tests/ls_proto/test_ls_proto"

"$ROOT/tests/ls_proto/test_ls_proto"
```

- [ ] **Step 2: 写失败测试（常规往返 + 错误分支）**

创建 `tests/ls_proto/test_ls_proto.c`：

```c
/* test_ls_proto.c —— ls_proto 协议层宿主单测(TDD, 规范 8-1/8-2)
 * 构建/运行: sh tests/ls_proto/run.sh
 * 覆盖(边界优先):
 *   1. ls_crc16 已知向量;
 *   2. ls_pack / ls_unpack 常规往返(类型/命令字/长度/载荷逐字节);
 *   3. 错误分支: 长度不足(-1)、包头错(-2)、data_len 不符(-3)、CRC 错(-4);
 *   4. 大小端互转 ls_swap_endian_16。 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ls_proto.h"

/* ---- 测试脚手架 ---- */
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

#define CHECK_TRUE(cond, msg) CHECK_EQ((cond) ? 1 : 0, 1, (msg))

/* ---- 本节无外部桩: ls_proto.c 零依赖 ---- */

/* 打包一个基础查询包, 供错误分支测试改写 */
static uint16_t build_query_packet(uint8_t *buf)
{
    ls_packet_t pkt;
    uint16_t len = 0;

    memset(&pkt, 0, sizeof(pkt));
    pkt.type     = (uint8_t)(LS_BASE_QUERY >> 8);
    pkt.cmd      = (uint8_t)(LS_BASE_QUERY & 0xFF);
    pkt.data_len = 1;
    pkt.data[0]  = 0xFF;
    pkt.pck_len  = LS_DATA_BASE_LEN + pkt.data_len;

    CHECK_EQ(ls_pack(&pkt, buf, &len), 0, "pack ret");
    return len;
}

static void test_crc16(void)
{
    const uint8_t v[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    /* CRC-16/CCITT-FALSE: "123456789" -> 0x29B1 */
    CHECK_EQ(ls_crc16(v, sizeof(v)), 0x29B1, "crc16 known vector");
}

static void test_swap_endian(void)
{
    CHECK_EQ(ls_swap_endian_16(0x1234), 0x3412, "swap16");
    CHECK_EQ(ls_swap_endian_16(0x00FF), 0xFF00, "swap16 00ff");
    CHECK_EQ(ls_swap_endian_16(0xFFFF), 0xFFFF, "swap16 ffff");
    CHECK_EQ(ls_swap_endian_16(0x0000), 0x0000, "swap16 0000");
}

static void test_pack_unpack_roundtrip(void)
{
    uint8_t buf[256];
    uint16_t len = 0;
    ls_packet_t out;

    len = build_query_packet(buf);
    CHECK_EQ(len, LS_DATA_BASE_LEN + 1, "packed len");

    /* 包头字符串逐字节 */
    CHECK_TRUE(memcmp(buf, LS_HEADER_STR, LS_HEADER_LEN) == 0, "header str");
    /* pck_len 大端 */
    CHECK_EQ(buf[13], 0x00, "pck_len hi");
    CHECK_EQ(buf[14], LS_DATA_BASE_LEN + 1, "pck_len lo");

    memset(&out, 0, sizeof(out));
    CHECK_EQ(ls_unpack(buf, len, &out), 0, "unpack ret");
    CHECK_EQ(out.pck_len, LS_DATA_BASE_LEN + 1, "unpacked pck_len");
    CHECK_EQ(out.version, LS_VERSION, "unpacked version");
    CHECK_EQ(out.type, (uint8_t)(LS_BASE_QUERY >> 8), "unpacked type");
    CHECK_EQ(out.cmd, (uint8_t)(LS_BASE_QUERY & 0xFF), "unpacked cmd");
    CHECK_EQ(out.data_len, 1, "unpacked data_len");
    CHECK_EQ(out.data[0], 0xFF, "unpacked payload");
}

static void test_unpack_errors(void)
{
    uint8_t buf[256];
    uint16_t len = build_query_packet(buf);
    ls_packet_t out;

    /* 长度不足 */
    CHECK_EQ(ls_unpack(buf, LS_DATA_BASE_LEN - 1, &out), -1, "too short");

    /* 包头错: 改首字节 */
    buf[0] = (uint8_t)(buf[0] ^ 0xFF);
    CHECK_EQ(ls_unpack(buf, len, &out), -2, "bad header");
    buf[0] = (uint8_t)(buf[0] ^ 0xFF);

    /* data_len 与 pck_len 不符: 把 data_len 字段改大 */
    buf[19] = 0x00;
    buf[20] = 0x02;
    CHECK_EQ(ls_unpack(buf, len, &out), -3, "len mismatch");
    buf[19] = 0x00;
    buf[20] = 0x01;

    /* CRC 错: 改载荷 */
    buf[21] = (uint8_t)(buf[21] ^ 0xFF);
    CHECK_EQ(ls_unpack(buf, len, &out), -4, "bad crc");
    buf[21] = (uint8_t)(buf[21] ^ 0xFF);

    /* NULL 入参 */
    CHECK_EQ(ls_unpack(NULL, len, &out), -1, "null in_buf");
    CHECK_EQ(ls_unpack(buf, len, NULL), -1, "null pkt");

    /* 修复后应恢复正常 */
    CHECK_EQ(ls_unpack(buf, len, &out), 0, "recovered");
}

int main(void)
{
    test_crc16();
    test_swap_endian();
    test_pack_unpack_roundtrip();
    test_unpack_errors();

    printf("ls_proto: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
```

- [ ] **Step 3: 运行测试**

Run: `sh tests/ls_proto/run.sh`（在 `I301_code-ad-da/` 仓库根执行）
Expected: 编译通过并输出 `ls_proto: N passed, 0 failed`

> 若 `ls_crc16` 已知向量那项失败，说明查表法与 CCITT-FALSE 不符 —— **停下来核对**：`ls_proto.c` 用的是查表法，多项式 0x1021、初值 0xFFFF、无反转，正是 CCITT-FALSE，期望值应为 0x29B1。若实测不同，以实测值为准修正该断言并在注释中记录（不得反过来改实现）。

- [ ] **Step 4: 提交（须先获用户批准）**

```bash
git add tests/ls_proto/run.sh tests/ls_proto/test_ls_proto.c
git commit -m "新增协议层宿主单测: 打包解包往返与错误分支基线"
```

---

## Task 2: `ls_proto_data.h` 新增命令字与结构体

**Files:**
- Modify: `I301_code/Proto/Inc/ls_proto_data.h`（在现有 `#pragma pack(1)` 区块内追加）
- Test: `tests/ls_proto/test_ls_proto.c`（追加一个测试函数）

**Interfaces:**
- Consumes: 无
- Produces: 后续 Task 3/4/6 依赖的类型与常量
  - `LS_BASE_REPLY_ALL`=0x0104、`LS_CTRL_SET_PROFILE`=0x0305、`LS_CTRL_GET_ALL`=0x0306、`LS_CTRL_FORCE_PROFILE`=0x0307
  - `LS_PROFILE_NONE`=0xFF、`LS_PROFILE_NUM`=3
  - `ls_profile_t`(10B)、`ls_ctrl_set_profile_t`(11B)、`ls_ctrl_get_all_t`(1B)、`ls_ctrl_force_t`(1B)、`ls_all_reply_t`(36B)

- [ ] **Step 1: 写失败测试（尺寸与命令字取值）**

在 `tests/ls_proto/test_ls_proto.c` 的 `main()` 之前追加：

```c
static void test_profile_types(void)
{
    /* 协议常量取值: 一旦被无意改动, 两仓镜像会静默错位 */
    CHECK_EQ(LS_BASE_REPLY_ALL,     0x0104, "cmd reply_all");
    CHECK_EQ(LS_CTRL_SET_PROFILE,   0x0305, "cmd set_profile");
    CHECK_EQ(LS_CTRL_GET_ALL,       0x0306, "cmd get_all");
    CHECK_EQ(LS_CTRL_FORCE_PROFILE, 0x0307, "cmd force_profile");
    CHECK_EQ(LS_PROFILE_NONE,       0xFF,   "profile none");
    CHECK_EQ(LS_PROFILE_NUM,        3,      "profile num");

    /* 结构体尺寸: 决定两仓字节流一致性, 必须逐字节对上 */
    CHECK_EQ(sizeof(ls_profile_t),           10, "sizeof ls_profile_t");
    CHECK_EQ(sizeof(ls_ctrl_set_profile_t),  11, "sizeof ls_ctrl_set_profile_t");
    CHECK_EQ(sizeof(ls_ctrl_get_all_t),       1, "sizeof ls_ctrl_get_all_t");
    CHECK_EQ(sizeof(ls_ctrl_force_t),         1, "sizeof ls_ctrl_force_t");
    CHECK_EQ(sizeof(ls_all_reply_t),         36, "sizeof ls_all_reply_t");

    /* 字段偏移: radc 在前、comp 在后, 不得因对齐改变 */
    CHECK_EQ(offsetof(ls_profile_t, radc),   0, "offset radc");
    CHECK_EQ(offsetof(ls_profile_t, comp_x), 6, "offset comp_x");
    CHECK_EQ(offsetof(ls_profile_t, comp_y), 8, "offset comp_y");
}
```

并在 `main()` 中追加调用 `test_profile_types();`（放在 `test_swap_endian();` 之后）。

同时把 `#include <stddef.h>` 加到该文件的 include 区（`offsetof` 需要）。

- [ ] **Step 2: 运行测试确认失败**

Run: `sh tests/ls_proto/run.sh`
Expected: 编译失败，报 `LS_BASE_REPLY_ALL` 未声明 / `ls_profile_t` 未定义

- [ ] **Step 3: 实现**

在 `I301_code/Proto/Inc/ls_proto_data.h` 中：

(a) 在 `ls_type_e` 枚举里追加新命令字：

```c
typedef enum
{
    LS_BASE_QUERY             = 0x0101,
    LS_BASE_REPLY             = 0x0102,
    LS_BASE_RESET_DEVICE      = 0x0103,
    LS_BASE_REPLY_ALL         = 0x0104,    /* 全量回复: 三套配置 + 工况 */

    LS_CTRL_REPLY             = 0x0301,
    LS_CTRL_RDAC              = 0x0302,    /* 控制数字电位器 */
    LS_CTRL_SET_COMP          = 0x0303,    /* 设置补偿值 */
    LS_CTRL_SAVE_PARAM        = 0x0304,    /* 参数保存 */
    LS_CTRL_SET_PROFILE       = 0x0305,    /* 整包写一套配置 */
    LS_CTRL_GET_ALL           = 0x0306,    /* 请求全量回读 */
    LS_CTRL_FORCE_PROFILE     = 0x0307,    /* 强制套 / 保活 / 解除 */

} ls_type_e;

/* 配置套: 三工况, 语义见 spec §3.1 */
#define LS_PROFILE_NUM   3
#define LS_PROFILE_NONE  0xFF   /* 强制解除(0x0307 专用) */
```

(b) 在 `#pragma pack(1)` 区块内、现有 `ls_ctrl_set_comp_t` 之后追加结构体：

```c
/* 一套配置(0x0305 写、0x0104 读共用, 10B)
 * 分层: 协议层自定义, 不依赖 App 层 param.h 类型(规范 13-3);
 *   字节布局与 param_profile_t 一致, 映射在 device_app 层做。
 *   radc 单字节无需转换; comp_x/comp_y 需大小端转换。 */
typedef struct
{
    uint8_t  radc[6];      /* X1 X2 X3 Y1 Y2 Y3, 各 0~255 */
    int16_t  comp_x;       /* X 轴补偿值, [-2000, 2000] */
    int16_t  comp_y;       /* Y 轴补偿值, [-2000, 2000] */
} ls_profile_t;

/* 控制类: 整包写一套(命令字 0x0305)
 *   profile : 0~2 目标套; 越界拒收 */
typedef struct
{
    uint8_t      profile;
    ls_profile_t data;
} ls_ctrl_set_profile_t;

/* 控制类: 请求全量回读(命令字 0x0306)
 *   req : 填 0xFF */
typedef struct
{
    uint8_t req;
} ls_ctrl_get_all_t;

/* 控制类: 强制套(命令字 0x0307), 同时用作保活帧
 *   profile : 0~2 强制到该套; 0xFF 解除强制 */
typedef struct
{
    uint8_t profile;
} ls_ctrl_force_t;

/* 基础类: 全量回复(命令字 0x0104, 36B)
 *   active_profile : 设备当前生效套(自主判定结果)
 *   forced_profile : 强制套; 0xFF = 未强制 */
typedef struct
{
    uint16_t     device_id;
    uint16_t     device_version;
    uint8_t      active_profile;
    uint8_t      forced_profile;
    ls_profile_t profiles[LS_PROFILE_NUM];
} ls_all_reply_t;
```

> ⚠️ 必须放在 `#pragma pack(1)` 与 `#pragma pack()` 之间，否则尺寸断言会失败。

- [ ] **Step 4: 运行测试确认通过**

Run: `sh tests/ls_proto/run.sh`
Expected: `ls_proto: N passed, 0 failed`

- [ ] **Step 5: 提交（须先获用户批准）**

```bash
git add I301_code/Proto/Inc/ls_proto_data.h tests/ls_proto/test_ls_proto.c
git commit -m "协议新增三套配置命令字与结构体: 整包读写/全量回读/强制套"
```

---

## Task 3: trans 层新增四个发送函数

**Files:**
- Modify: `I301_code/Proto/Inc/ls_proto_trans.h`
- Modify: `I301_code/Proto/Src/ls_proto_trans.c`
- Test: `tests/ls_proto/test_ls_proto.c`（追加测试函数）

**Interfaces:**
- Consumes: Task 2 的类型；现有的 `s_pack_and_send()`、`s_work_pkt`、`s_work_buf`、`s_work_len`、`ls_trans_init_callbacks()`
- Produces:
  - `int ls_ctrl_set_profile(uint8_t profile, const ls_profile_t *p)`
  - `int ls_ctrl_get_all(void)`
  - `int ls_ctrl_force_profile(uint8_t profile)`
  - `int ls_base_reply_all(void)`
  - `ls_cb_get_device_info_all_t` 类型，及 `ls_trans_callbacks_t` 新成员 `get_device_info_all`

- [ ] **Step 1: 写失败测试（发送内容逐字节校验）**

在 `tests/ls_proto/test_ls_proto.c` 中追加（放在 `test_profile_types` 之后）：

```c
/* ---- 发送捕获桩: 挂到 trans 层 send 回调上 ---- */
static uint8_t  s_tx_buf[1024];
static uint16_t s_tx_len = 0;
static int      s_tx_count = 0;

static void stub_send(uint8_t *buf, uint16_t len)
{
    if (len > sizeof(s_tx_buf)) { len = sizeof(s_tx_buf); }
    memcpy(s_tx_buf, buf, len);
    s_tx_len = len;
    s_tx_count++;
}

/* 全量回复的数据源桩 */
static ls_all_reply_t s_stub_all;

static void stub_get_device_info_all(ls_all_reply_t *reply)
{
    *reply = s_stub_all;
}

static void reset_tx(void)
{
    s_tx_len = 0;
    s_tx_count = 0;
    memset(s_tx_buf, 0, sizeof(s_tx_buf));
}

/* 从捕获到的字节流中解出包 */
static int tx_unpack(ls_packet_t *out)
{
    return ls_unpack(s_tx_buf, s_tx_len, out);
}

static void test_send_set_profile(void)
{
    static const ls_trans_callbacks_t cbs = { .send = stub_send };
    ls_trans_init_callbacks(&cbs);

    ls_profile_t p;
    memset(&p, 0, sizeof(p));
    p.radc[0] = 11; p.radc[1] = 22; p.radc[2] = 33;
    p.radc[3] = 44; p.radc[4] = 55; p.radc[5] = 66;
    p.comp_x  = -80;
    p.comp_y  = 1234;

    reset_tx();
    CHECK_EQ(ls_ctrl_set_profile(2, &p), 0, "set_profile ret");
    CHECK_EQ(s_tx_count, 1, "sent once");

    ls_packet_t out;
    CHECK_EQ(tx_unpack(&out), 0, "tx unpack");
    CHECK_EQ(out.type, (uint8_t)(LS_CTRL_SET_PROFILE >> 8), "tx type");
    CHECK_EQ(out.cmd,  (uint8_t)(LS_CTRL_SET_PROFILE & 0xFF), "tx cmd");
    CHECK_EQ(out.data_len, sizeof(ls_ctrl_set_profile_t), "tx data_len");
    CHECK_EQ(out.data[0], 2, "tx profile index");

    /* radc 单字节原样 */
    CHECK_EQ(out.data[1], 11, "tx radc x1");
    CHECK_EQ(out.data[6], 66, "tx radc y3");

    /* comp 大端: -80 = 0xFFB0 -> 字节序 FF B0 */
    CHECK_EQ(out.data[7],  0xFF, "tx comp_x hi");
    CHECK_EQ(out.data[8],  0xB0, "tx comp_x lo");
    /* 1234 = 0x04D2 -> 04 D2 */
    CHECK_EQ(out.data[9],  0x04, "tx comp_y hi");
    CHECK_EQ(out.data[10], 0xD2, "tx comp_y lo");

    /* NULL 指针拒发 */
    reset_tx();
    CHECK_EQ(ls_ctrl_set_profile(0, NULL), -1, "set_profile null");
    CHECK_EQ(s_tx_count, 0, "no send on null");
}

static void test_send_get_all_and_force(void)
{
    static const ls_trans_callbacks_t cbs = { .send = stub_send };
    ls_trans_init_callbacks(&cbs);

    ls_packet_t out;

    reset_tx();
    CHECK_EQ(ls_ctrl_get_all(), 0, "get_all ret");
    CHECK_EQ(tx_unpack(&out), 0, "get_all unpack");
    CHECK_EQ(out.cmd, (uint8_t)(LS_CTRL_GET_ALL & 0xFF), "get_all cmd");
    CHECK_EQ(out.data_len, 1, "get_all data_len");
    CHECK_EQ(out.data[0], 0xFF, "get_all payload");

    reset_tx();
    CHECK_EQ(ls_ctrl_force_profile(1), 0, "force ret");
    CHECK_EQ(tx_unpack(&out), 0, "force unpack");
    CHECK_EQ(out.cmd, (uint8_t)(LS_CTRL_FORCE_PROFILE & 0xFF), "force cmd");
    CHECK_EQ(out.data[0], 1, "force profile");

    reset_tx();
    CHECK_EQ(ls_ctrl_force_profile(LS_PROFILE_NONE), 0, "force release ret");
    CHECK_EQ(tx_unpack(&out), 0, "force release unpack");
    CHECK_EQ(out.data[0], 0xFF, "force release payload");
}

static void test_send_base_reply_all(void)
{
    static const ls_trans_callbacks_t cbs = {
        .send                = stub_send,
        .get_device_info_all = stub_get_device_info_all,
    };
    ls_trans_init_callbacks(&cbs);

    memset(&s_stub_all, 0, sizeof(s_stub_all));
    s_stub_all.device_id      = 0x1234;
    s_stub_all.device_version = 0x5678;
    s_stub_all.active_profile = 1;
    s_stub_all.forced_profile = LS_PROFILE_NONE;
    s_stub_all.profiles[0].radc[0] = 40;
    s_stub_all.profiles[2].radc[5] = 45;
    s_stub_all.profiles[1].comp_x  = -80;

    reset_tx();
    CHECK_EQ(ls_base_reply_all(), 0, "reply_all ret");

    ls_packet_t out;
    CHECK_EQ(tx_unpack(&out), 0, "reply_all unpack");
    CHECK_EQ(out.type, (uint8_t)(LS_BASE_REPLY_ALL >> 8), "reply_all type");
    CHECK_EQ(out.cmd,  (uint8_t)(LS_BASE_REPLY_ALL & 0xFF), "reply_all cmd");
    CHECK_EQ(out.data_len, sizeof(ls_all_reply_t), "reply_all data_len");

    /* 头部字段大端 */
    CHECK_EQ(out.data[0], 0x12, "id hi");
    CHECK_EQ(out.data[1], 0x34, "id lo");
    CHECK_EQ(out.data[2], 0x56, "ver hi");
    CHECK_EQ(out.data[3], 0x78, "ver lo");
    CHECK_EQ(out.data[4], 1,    "active");
    CHECK_EQ(out.data[5], 0xFF, "forced");

    /* profiles[0].radc[0] 在偏移 6+0 */
    CHECK_EQ(out.data[6], 40, "p0 radc x1");
    /* profiles[2].radc[5] 在偏移 6 + 2*10 + 5 */
    CHECK_EQ(out.data[31], 45, "p2 radc y3");
    /* profiles[1].comp_x = -80 -> 0xFFB0, 偏移 6 + 1*10 + 6 */
    CHECK_EQ(out.data[22], 0xFF, "p1 comp_x hi");
    CHECK_EQ(out.data[23], 0xB0, "p1 comp_x lo");
}
```

并在 `main()` 中追加：

```c
    test_send_set_profile();
    test_send_get_all_and_force();
    test_send_base_reply_all();
```

- [ ] **Step 2: 运行测试确认失败**

Run: `sh tests/ls_proto/run.sh`
Expected: 编译失败，报 `ls_ctrl_set_profile` / `ls_ctrl_get_all` / `ls_ctrl_force_profile` / `ls_base_reply_all` 未定义，`get_device_info_all` 无此成员

- [ ] **Step 3: 实现 — 头文件**

在 `I301_code/Proto/Inc/ls_proto_trans.h` 中：

(a) 在 `ls_cb_get_device_info_t` 之后追加回调类型：

```c
/* 全量回复：取三套配置与当前工况(spec §4.2) */
typedef void (*ls_cb_get_device_info_all_t)(ls_all_reply_t *reply);
```

(b) 在 `ls_trans_callbacks_t` 中追加成员：

```c
typedef struct
{
    ls_cb_send_t      send;           /* 必须注册：字节流发送函数     */

    ls_cb_get_device_info_t get_device_info; /* 查询包：获取设备信息 */

    ls_cb_get_device_info_all_t get_device_info_all; /* 全量包：取三套配置+工况 */

} ls_trans_callbacks_t;
```

(c) 在 `/* 控制类 */` 段追加函数声明：

```c
/* 控制类 */
int ls_ctrl_reply(uint16_t typeCMD);
int ls_ctrl_rdac(ls_radc_xy_e xy, ls_radc_ch_e ch, uint8_t code);
int ls_ctrl_set_comp(ls_radc_xy_e xy, int16_t value);
int ls_ctrl_save_param(void);
int ls_ctrl_set_profile(uint8_t profile, const ls_profile_t *p); /* 整包写一套 */
int ls_ctrl_get_all(void);                                       /* 请求全量回读 */
int ls_ctrl_force_profile(uint8_t profile);                      /* 强制套/解除 */

/* 基础类 */
int ls_base_reply_all(void);                                     /* 全量回复 */
```

> `ls_proto_trans.h` 已 `#include "ls_proto.h"`，后者包含 `ls_proto_data.h`，故新类型可见。

- [ ] **Step 4: 实现 — 实现文件**

在 `I301_code/Proto/Src/ls_proto_trans.c` 中，于 `ls_ctrl_save_param()` 之后追加：

```c
/**
 * @brief 发送整包写入一套配置(0x0305)
 * @param profile 目标套 0~2(越界由设备侧拒收)
 * @param p       该套的码值与补偿值
 * @return 0 成功，<0 失败
 *
 * radc 为单字节无需转换；comp_x/comp_y 转大端。
 */
int ls_ctrl_set_profile(uint8_t profile, const ls_profile_t *p)
{
    if (p == NULL)
    {
        return -1;
    }

    memset(&s_work_pkt, 0, sizeof(s_work_pkt));

    ls_ctrl_set_profile_t msg;
    msg.profile = profile;
    msg.data    = *p;

#if LS_ENDIAN_ENABLE
    msg.data.comp_x = (int16_t)ls_swap_endian_16((uint16_t)msg.data.comp_x);
    msg.data.comp_y = (int16_t)ls_swap_endian_16((uint16_t)msg.data.comp_y);
#endif

    s_work_pkt.type     = LS_CTRL_SET_PROFILE >> 8;
    s_work_pkt.cmd      = LS_CTRL_SET_PROFILE & 0xFF;
    s_work_pkt.data_len = sizeof(ls_ctrl_set_profile_t);
    memcpy(s_work_pkt.data, &msg, sizeof(ls_ctrl_set_profile_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;
    }
    return s_pack_and_send();
}

/**
 * @brief 发送全量回读请求(0x0306)
 * @return 0 成功，<0 失败
 */
int ls_ctrl_get_all(void)
{
    memset(&s_work_pkt, 0, sizeof(s_work_pkt));

    ls_ctrl_get_all_t msg;
    msg.req = 0xFF;

    s_work_pkt.type     = LS_CTRL_GET_ALL >> 8;
    s_work_pkt.cmd      = LS_CTRL_GET_ALL & 0xFF;
    s_work_pkt.data_len = sizeof(ls_ctrl_get_all_t);
    memcpy(s_work_pkt.data, &msg, sizeof(ls_ctrl_get_all_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;
    }
    return s_pack_and_send();
}

/**
 * @brief 发送强制套命令(0x0307)；同一帧用于保活
 * @param profile 0~2 强制到该套；LS_PROFILE_NONE(0xFF) 解除强制
 * @return 0 成功，<0 失败
 */
int ls_ctrl_force_profile(uint8_t profile)
{
    memset(&s_work_pkt, 0, sizeof(s_work_pkt));

    ls_ctrl_force_t msg;
    msg.profile = profile;

    s_work_pkt.type     = LS_CTRL_FORCE_PROFILE >> 8;
    s_work_pkt.cmd      = LS_CTRL_FORCE_PROFILE & 0xFF;
    s_work_pkt.data_len = sizeof(ls_ctrl_force_t);
    memcpy(s_work_pkt.data, &msg, sizeof(ls_ctrl_force_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;
    }
    return s_pack_and_send();
}
```

在 `ls_base_reply()` 之后追加：

```c
/**
 * @brief 发送全量回复包(0x0104)：设备信息 + 三套配置 + 当前工况
 * @return 0 成功，<0 失败
 */
int ls_base_reply_all(void)
{
    ls_all_reply_t reply;
    uint8_t i;

    memset(&reply, 0, sizeof(reply));

    /* 回调从设备中获取三套配置与工况 */
    if (s_cbs && s_cbs->get_device_info_all)
    {
        s_cbs->get_device_info_all(&reply);
    }
    else
    {
        LS_LOG_INFO("ls - get_device_info_all callback not registered, reply empty.");
    }

    s_work_pkt.type     = LS_BASE_REPLY_ALL >> 8;
    s_work_pkt.cmd      = LS_BASE_REPLY_ALL & 0xFF;
    s_work_pkt.data_len = sizeof(ls_all_reply_t);

#if LS_ENDIAN_ENABLE     /* 小端转大端 */
    reply.device_id      = ls_swap_endian_16(reply.device_id);
    reply.device_version = ls_swap_endian_16(reply.device_version);
    for (i = 0U; i < LS_PROFILE_NUM; i++)
    {
        reply.profiles[i].comp_x =
            (int16_t)ls_swap_endian_16((uint16_t)reply.profiles[i].comp_x);
        reply.profiles[i].comp_y =
            (int16_t)ls_swap_endian_16((uint16_t)reply.profiles[i].comp_y);
    }
#endif

    memcpy(s_work_pkt.data, &reply, sizeof(ls_all_reply_t));

    s_work_pkt.pck_len = LS_DATA_BASE_LEN + s_work_pkt.data_len;
    if (ls_pack(&s_work_pkt, s_work_buf, &s_work_len) < 0)
    {
        return -1;
    }
    return s_pack_and_send();
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `sh tests/ls_proto/run.sh`
Expected: `ls_proto: N passed, 0 failed`

- [ ] **Step 6: 提交（须先获用户批准）**

```bash
git add I301_code/Proto/Inc/ls_proto_trans.h I301_code/Proto/Src/ls_proto_trans.c tests/ls_proto/test_ls_proto.c
git commit -m "协议发送层新增整包写配置/全量回读/强制套与全量回复"
```

---

## Task 4: receive 层新增四个处理函数与回调

**Files:**
- Modify: `I301_code/Proto/Inc/ls_proto_receive.h`
- Modify: `I301_code/Proto/Src/ls_proto_receive.c`
- Test: `tests/ls_proto/test_ls_proto.c`（追加测试函数）

**Interfaces:**
- Consumes: Task 2 类型；Task 3 的 `ls_base_reply_all()`、`ls_ctrl_reply()`；现有的 `s_cbs`、`ls_handle()`
- Produces: 供 Task 6 实现的新回调成员
  - `ls_cb_ctrl_set_profile_t ctrl_set_profile(const ls_ctrl_set_profile_t *)` → `int`
  - `ls_cb_ctrl_get_all_t ctrl_get_all(void)` → `int`
  - `ls_cb_ctrl_force_t ctrl_force_profile(const ls_ctrl_force_t *)` → `int`
  - `ls_cb_on_reply_all_t on_reply_all(const ls_all_reply_t *)` → `void`

- [ ] **Step 1: 写失败测试（设备侧解析 + 主机侧全量回包解析）**

在 `tests/ls_proto/test_ls_proto.c` 中追加：

```c
/* ---- 接收回调桩 ---- */
static ls_ctrl_set_profile_t s_rx_set_profile;
static int  s_rx_set_profile_n = 0;
static int  s_rx_set_profile_ret = 0;

static int stub_ctrl_set_profile(const ls_ctrl_set_profile_t *m)
{
    s_rx_set_profile = *m;
    s_rx_set_profile_n++;
    return s_rx_set_profile_ret;
}

static int s_rx_get_all_n = 0;
static int stub_ctrl_get_all(void) { s_rx_get_all_n++; return 0; }

static ls_ctrl_force_t s_rx_force;
static int s_rx_force_n = 0;
static int stub_ctrl_force_profile(const ls_ctrl_force_t *m)
{
    s_rx_force = *m;
    s_rx_force_n++;
    return 0;
}

static ls_all_reply_t s_rx_all;
static int s_rx_all_n = 0;
static void stub_on_reply_all(const ls_all_reply_t *r)
{
    s_rx_all = *r;
    s_rx_all_n++;
}

/* 造一个可被 ls_parse 解析的整包 */
static uint16_t make_packet(uint16_t typeCmd, const void *payload, uint16_t payload_len,
                            uint8_t *buf)
{
    ls_packet_t pkt;
    uint16_t len = 0;

    memset(&pkt, 0, sizeof(pkt));
    pkt.type     = (uint8_t)(typeCmd >> 8);
    pkt.cmd      = (uint8_t)(typeCmd & 0xFF);
    pkt.data_len = payload_len;
    if (payload_len > 0U)
    {
        memcpy(pkt.data, payload, payload_len);
    }
    pkt.pck_len  = LS_DATA_BASE_LEN + payload_len;

    CHECK_EQ(ls_pack(&pkt, buf, &len), 0, "make_packet pack");
    return len;
}

static void test_handle_set_profile(void)
{
    ls_packet_t pkt;   /* 声明于外侧会被下面复用, 便于逐分支校验 */

    /* 先初始化发送桩, 否则 ls_ctrl_reply 无法发出 */
    static const ls_trans_callbacks_t t_cbs = { .send = stub_send };
    ls_trans_init_callbacks(&t_cbs);

    static const ls_receive_callbacks_t r_cbs = {
        .ctrl_set_profile = stub_ctrl_set_profile,
    };
    ls_receiver_init_callbacks(&r_cbs);

    /* 造一个设备侧会收到的写配置包: 载荷为【大端】comp */
    ls_ctrl_set_profile_t in;
    memset(&in, 0, sizeof(in));
    in.profile  = 2;
    in.data.radc[0] = 11;
    in.data.radc[5] = 66;
    in.data.comp_x  = (int16_t)ls_swap_endian_16((uint16_t)(int16_t)(-80));
    in.data.comp_y  = (int16_t)ls_swap_endian_16((uint16_t)(int16_t)1234);

    uint8_t buf[256];
    uint16_t len = make_packet(LS_CTRL_SET_PROFILE, &in, sizeof(in), buf);

    s_rx_set_profile_n = 0;
    reset_tx();
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse set_profile");
    CHECK_EQ(s_rx_set_profile_n, 1, "callback invoked");
    CHECK_EQ(s_rx_set_profile.profile, 2, "parsed profile");
    CHECK_EQ(s_rx_set_profile.data.radc[0], 11, "parsed radc x1");
    CHECK_EQ(s_rx_set_profile.data.radc[5], 66, "parsed radc y3");
    CHECK_EQ(s_rx_set_profile.data.comp_x, -80,  "parsed comp_x (endian)");
    CHECK_EQ(s_rx_set_profile.data.comp_y, 1234, "parsed comp_y (endian)");
    /* 回送控制应答 */
    CHECK_EQ(s_tx_count, 1, "acked once");

    /* 长度不符 → 拒收且不回调 */
    s_rx_set_profile_n = 0;
    len = make_packet(LS_CTRL_SET_PROFILE, &in, sizeof(in) - 1U, buf);
    CHECK_EQ(ls_parse(&pkt, buf, len), -1, "short payload rejected");
    CHECK_EQ(s_rx_set_profile_n, 0, "no callback on bad len");
}

static void test_handle_get_all_and_force(void)
{
    static const ls_trans_callbacks_t t_cbs = {
        .send                = stub_send,
        .get_device_info_all = stub_get_device_info_all,
    };
    ls_trans_init_callbacks(&t_cbs);

    static const ls_receive_callbacks_t r_cbs = {
        .ctrl_get_all      = stub_ctrl_get_all,
        .ctrl_force_profile = stub_ctrl_force_profile,
    };
    ls_receiver_init_callbacks(&r_cbs);

    ls_packet_t pkt;
    uint8_t buf[256];
    uint16_t len;

    /* 0x0306 → 控制应答 + 全量回复, 共两包 */
    ls_ctrl_get_all_t req;
    req.req = 0xFF;
    memset(&s_stub_all, 0, sizeof(s_stub_all));
    s_stub_all.active_profile = 1;
    s_stub_all.profiles[1].radc[0] = 120;

    len = make_packet(LS_CTRL_GET_ALL, &req, sizeof(req), buf);
    s_rx_get_all_n = 0;
    reset_tx();
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse get_all");
    CHECK_EQ(s_rx_get_all_n, 1, "get_all callback");
    CHECK_EQ(s_tx_count, 2, "acked + full reply");
    CHECK_EQ(tx_unpack(&pkt), 0, "second packet unpack");
    CHECK_EQ(pkt.cmd, (uint8_t)(LS_BASE_REPLY_ALL & 0xFF), "second is reply_all");
    CHECK_EQ(pkt.data[4], 1, "active in reply_all");
    CHECK_EQ(pkt.data[16], 120, "p1 radc x1 in reply_all");

    /* 0x0307 强制 */
    ls_ctrl_force_t fo;
    fo.profile = 2;
    len = make_packet(LS_CTRL_FORCE_PROFILE, &fo, sizeof(fo), buf);
    s_rx_force_n = 0;
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse force");
    CHECK_EQ(s_rx_force_n, 1, "force callback");
    CHECK_EQ(s_rx_force.profile, 2, "parsed force profile");

    /* 0x0307 解除 */
    fo.profile = LS_PROFILE_NONE;
    len = make_packet(LS_CTRL_FORCE_PROFILE, &fo, sizeof(fo), buf);
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse force release");
    CHECK_EQ(s_rx_force.profile, LS_PROFILE_NONE, "parsed release");
}

static void test_handle_base_reply_all(void)
{
    static const ls_trans_callbacks_t t_cbs = { .send = stub_send };
    ls_trans_init_callbacks(&t_cbs);

    static const ls_receive_callbacks_t r_cbs = {
        .on_reply_all = stub_on_reply_all,
    };
    ls_receiver_init_callbacks(&r_cbs);

    /* 主机侧收到的是【大端】载荷 */
    ls_all_reply_t in;
    memset(&in, 0, sizeof(in));
    in.device_id      = ls_swap_endian_16(0x1234);
    in.device_version = ls_swap_endian_16(0x5678);
    in.active_profile = 1;
    in.forced_profile = 2;
    in.profiles[0].radc[0] = 40;
    in.profiles[2].comp_y  = (int16_t)ls_swap_endian_16((uint16_t)(int16_t)(-95));

    ls_packet_t pkt;
    uint8_t buf[256];
    uint16_t len = make_packet(LS_BASE_REPLY_ALL, &in, sizeof(in), buf);

    s_rx_all_n = 0;
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse reply_all");
    CHECK_EQ(s_rx_all_n, 1, "on_reply_all invoked");
    CHECK_EQ(s_rx_all.device_id, 0x1234, "parsed device_id (endian)");
    CHECK_EQ(s_rx_all.device_version, 0x5678, "parsed device_version (endian)");
    CHECK_EQ(s_rx_all.active_profile, 1, "parsed active");
    CHECK_EQ(s_rx_all.forced_profile, 2, "parsed forced");
    CHECK_EQ(s_rx_all.profiles[0].radc[0], 40, "parsed p0 radc x1");
    CHECK_EQ(s_rx_all.profiles[2].comp_y, -95, "parsed p2 comp_y (endian)");
}
```

并在 `main()` 中追加：

```c
    test_handle_set_profile();
    test_handle_get_all_and_force();
    test_handle_base_reply_all();
```

- [ ] **Step 2: 运行测试确认失败**

Run: `sh tests/ls_proto/run.sh`
Expected: 编译失败，报 `ctrl_set_profile` / `ctrl_get_all` / `ctrl_force_profile` / `on_reply_all` 无此成员，`handle_ctrl_set_profile` 等未定义

- [ ] **Step 3: 实现 — 头文件**

在 `I301_code/Proto/Inc/ls_proto_receive.h` 中：

(a) 在 `ls_cb_ctrl_save_param_t` 之后追加回调类型：

```c
/* 整包写一套配置包回调：返回 0 表示成功，非 0 表示失败 */
typedef int  (*ls_cb_ctrl_set_profile_t)(const ls_ctrl_set_profile_t *msg);

/* 全量回读请求包回调：返回 0 表示成功，非 0 表示失败 */
typedef int  (*ls_cb_ctrl_get_all_t)(void);

/* 强制套包回调：返回 0 表示成功，非 0 表示失败 */
typedef int  (*ls_cb_ctrl_force_t)(const ls_ctrl_force_t *msg);
```

(b) 在 `ls_cb_on_ctrl_reply_t` 之后追加：

```c
/* 全量回复包回调：收到设备的三套配置与工况快照 */
typedef void (*ls_cb_on_reply_all_t)(const ls_all_reply_t *reply);
```

(c) 更新回调集合结构体：

```c
typedef struct
{
    /**  设备  **/
    ls_cb_reset_device_t      reset_device;      /* 重启包：重启设备 */
    ls_cb_ctrl_rdac_t         ctrl_rdac;         /* 控制包：设置数字电位器 */
    ls_cb_ctrl_set_comp_t     ctrl_set_comp;     /* 控制包：设置补偿值 */
    ls_cb_ctrl_save_param_t   ctrl_save_param;   /* 控制包：参数保存 */
    ls_cb_ctrl_set_profile_t  ctrl_set_profile;  /* 控制包：整包写一套配置 */
    ls_cb_ctrl_get_all_t      ctrl_get_all;      /* 控制包：请求全量回读 */
    ls_cb_ctrl_force_t        ctrl_force_profile;/* 控制包：强制套 */

    /**  主机  **/
    ls_cb_on_reply_t          on_reply;          /* 回复包：处理回复数据 */
    ls_cb_on_ctrl_reply_t     on_ctrl_reply;     /* 控制包：应答包处理 */
    ls_cb_on_reply_all_t      on_reply_all;      /* 全量回复包：三套配置+工况 */
} ls_receive_callbacks_t;
```

(d) 在 handler 声明区追加：

```c
/* 基础类 */
int handle_base_query(void);
int handle_base_reply(ls_packet_t *pkt);
int handle_base_reset_device(void);
int handle_base_reply_all(ls_packet_t *pkt);

/* 控制类 */
int handle_ctrl_reply(ls_packet_t *pkt);
int handle_ctrl_rdac(ls_packet_t *pkt);
int handle_ctrl_set_comp(ls_packet_t *pkt);
int handle_ctrl_save_param(ls_packet_t *pkt);
int handle_ctrl_set_profile(ls_packet_t *pkt);
int handle_ctrl_get_all(ls_packet_t *pkt);
int handle_ctrl_force_profile(ls_packet_t *pkt);
```

- [ ] **Step 4: 实现 — 实现文件**

在 `I301_code/Proto/Src/ls_proto_receive.c` 中：

(a) 在 `ls_handle()` 的 switch 中追加 case：

```c
        case LS_BASE_REPLY_ALL:
            return handle_base_reply_all(pkt);
```
（放在 `case LS_BASE_REPLY:` 之后）

```c
        case LS_CTRL_SET_PROFILE:
            return handle_ctrl_set_profile(pkt);
        case LS_CTRL_GET_ALL:
            return handle_ctrl_get_all(pkt);
        case LS_CTRL_FORCE_PROFILE:
            return handle_ctrl_force_profile(pkt);
```
（放在 `case LS_CTRL_SAVE_PARAM:` 之后）

(b) 在 `handle_base_reply()` 之后追加主机侧全量回包处理：

```c
/**
 * @brief 主机收到全量回复包(0x0104)：解出三套配置与工况后交回调
 * @return 0 成功，<0 失败
*/
int handle_base_reply_all(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received base reply all.");

    if (pkt->data_len != sizeof(ls_all_reply_t))
    {
        LS_LOG_INFO("ls - base reply all data_len err: %u", pkt->data_len);
        return -1;
    }

    ls_all_reply_t reply;
    uint8_t i;

    memcpy(&reply, pkt->data, sizeof(ls_all_reply_t));

#if LS_RX_ENDIAN_ENABLE     // 大小端互转
    reply.device_id      = ls_swap_endian_16(reply.device_id);
    reply.device_version = ls_swap_endian_16(reply.device_version);
    for (i = 0U; i < LS_PROFILE_NUM; i++)
    {
        reply.profiles[i].comp_x =
            (int16_t)ls_swap_endian_16((uint16_t)reply.profiles[i].comp_x);
        reply.profiles[i].comp_y =
            (int16_t)ls_swap_endian_16((uint16_t)reply.profiles[i].comp_y);
    }
#endif

    if (s_cbs && s_cbs->on_reply_all)
    {
        s_cbs->on_reply_all(&reply);
    }
    else
    {
        LS_LOG_INFO("ls - on_reply_all callback not registered, ignored.");
    }
    return 0;
}
```

(c) 在文件末尾（`handle_ctrl_save_param()` 之后）追加三个设备侧处理函数：

```c
/**
 * @brief 设备收到整包写配置包(0x0305)：解析载荷并下发到应用层，
 *        随后回送控制应答包(0x0301)，data 字段填入 0x0305。
 * @return 0 成功，<0 失败
 */
int handle_ctrl_set_profile(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl set profile.");

    /* 数据长度校验：协议规定 11 字节(profile 1 + radc 6 + comp 4) */
    if (pkt->data_len != sizeof(ls_ctrl_set_profile_t))
    {
        LS_LOG_INFO("ls - ctrl set profile data_len err: %u", pkt->data_len);
        return -1;
    }

    ls_ctrl_set_profile_t msg;
    memcpy(&msg, pkt->data, sizeof(ls_ctrl_set_profile_t));

#if LS_RX_ENDIAN_ENABLE
    msg.data.comp_x = (int16_t)ls_swap_endian_16((uint16_t)msg.data.comp_x);
    msg.data.comp_y = (int16_t)ls_swap_endian_16((uint16_t)msg.data.comp_y);
#endif

    int ret = 0;
    if (s_cbs && s_cbs->ctrl_set_profile)
    {
        ret = s_cbs->ctrl_set_profile(&msg);
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_set_profile callback not registered, ignored.");
        ret = -1;
    }

    /* 不论成功失败，按协议返回控制应答包 */
    (void)ls_ctrl_reply(LS_CTRL_SET_PROFILE);
    return ret;
}

/**
 * @brief 设备收到全量回读请求(0x0306)：先回控制应答，再回全量回复包。
 * @return 0 成功，<0 失败
 */
int handle_ctrl_get_all(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl get all.");
    (void)pkt;

    int ret = 0;
    if (s_cbs && s_cbs->ctrl_get_all)
    {
        ret = s_cbs->ctrl_get_all();
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_get_all callback not registered, ignored.");
        ret = -1;
    }

    /* 按协议返回控制应答包，随后附上全量数据 */
    (void)ls_ctrl_reply(LS_CTRL_GET_ALL);
    (void)ls_base_reply_all();
    return ret;
}

/**
 * @brief 设备收到强制套包(0x0307)：解析并下发；
 *        profile == LS_PROFILE_NONE 表示解除强制。
 * @return 0 成功，<0 失败
 */
int handle_ctrl_force_profile(ls_packet_t *pkt)
{
    LS_LOG_INFO("ls - received ctrl force profile.");

    if (pkt->data_len != sizeof(ls_ctrl_force_t))
    {
        LS_LOG_INFO("ls - ctrl force profile data_len err: %u", pkt->data_len);
        return -1;
    }

    ls_ctrl_force_t msg;
    memcpy(&msg, pkt->data, sizeof(ls_ctrl_force_t));

    int ret = 0;
    if (s_cbs && s_cbs->ctrl_force_profile)
    {
        ret = s_cbs->ctrl_force_profile(&msg);
    }
    else
    {
        LS_LOG_INFO("ls - ctrl_force_profile callback not registered, ignored.");
        ret = -1;
    }

    (void)ls_ctrl_reply(LS_CTRL_FORCE_PROFILE);
    return ret;
}
```

> `ls_proto_receive.c` 已 `#include "ls_proto_trans.h"`，故 `ls_ctrl_reply()` / `ls_base_reply_all()` 可见。

- [ ] **Step 5: 运行测试确认通过**

Run: `sh tests/ls_proto/run.sh`
Expected: `ls_proto: N passed, 0 failed`

- [ ] **Step 6: 提交（须先获用户批准）**

```bash
git add I301_code/Proto/Inc/ls_proto_receive.h I301_code/Proto/Src/ls_proto_receive.c tests/ls_proto/test_ls_proto.c
git commit -m "协议接收层新增整包写配置/全量回读/强制套处理与全量回包解析"
```

---

## Task 5: `param` 层从单套扩为三套

**Files:**
- Modify: `I301_code/App/param.h`
- Modify: `I301_code/App/param.c`
- Create: `tests/param/run.sh`
- Create: `tests/param/test_param.c`

**Interfaces:**
- Consumes: `bsp_flash_save/load`、`ad5290_set_all_code`、`ad5290_init`、`port_delay_ms`（均为既有接口，测试中以桩替代）
- Produces: 供 Task 6 使用的新接口
  - `PARAM_PROFILE_NUM`=3、`PARAM_PROFILE_NONE`=0xFF
  - `param_profile_t`、`flash_store_t`（30B）
  - `const param_profile_t *param_profile(uint8_t idx)`
  - `int param_profile_set(uint8_t idx, const param_profile_t *p)`
  - `int param_force_set(uint8_t idx)`
  - `int param_force_clear(void)`
  - `uint8_t param_active(void)`
  - `uint8_t param_forced(void)`
  - 既有接口语义保持：`param_radc_get/set`、`param_set_comp_x/y`、`param_comp`、`param_save` 均作用于**当前生效套**

- [ ] **Step 1: 写失败测试**

创建 `tests/param/run.sh`：

```sh
#!/bin/sh
# run.sh —— param 参数层宿主单测构建与运行(规范 8-1: 宿主测试先行)
# 用法: 仓库根目录执行  sh tests/param/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

GCC="${GCC:-/c/Qt/Tools/mingw1310_64/bin/gcc.exe}"

"$GCC" -std=c99 -Wall -Wextra -O2 \
    -I "$ROOT/I301_code/App" \
    -I "$ROOT/I301_code/Bsp" \
    -I "$ROOT/I301_code/Port" \
    "$ROOT/tests/param/test_param.c" \
    "$ROOT/I301_code/App/param.c" \
    -o "$ROOT/tests/param/test_param"

"$ROOT/tests/param/test_param"
```

创建 `tests/param/test_param.c`：

```c
/* test_param.c —— param 参数层宿主单测(TDD, 规范 8-1/8-2)
 * 构建/运行: sh tests/param/run.sh
 * 覆盖(边界优先):
 *   1. 三套存储互不串扰(写第 2 套不动第 0/1 套);
 *   2. 索引越界拒收;
 *   3. comp 范围越界回退默认;
 *   4. 生效套切换后 param_radc_get/param_comp 跟随;
 *   5. 强制优先于自主生效;
 *   6. flash_store_t 尺寸 = 30 字节。 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "param.h"
#include "ad5290.h"

/* ---- 桩: ad5290 ---- */
static uint8_t s_pot[AD5290_TOTAL_NUM];
static int     s_pot_all_n = 0;

void ad5290_init(void) { }
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
    (void)axis; (void)ch; (void)ohm;
}
void ad5290_set_all_ohm(const float ohms[AD5290_TOTAL_NUM]) { (void)ohms; }
uint8_t ad5290_get_code(ad5290_axis_e axis, ad5290_ch_e ch)
{
    return s_pot[(int)axis * AD5290_CH_PER_AXIS + (int)ch];
}

/* ---- 桩: port_tick ---- */
void port_delay_ms(uint32_t ms) { (void)ms; }

/* ---- 桩: 日志(LOG_ENABLE_SYS=1 会引用) ---- */
typedef enum {
    LOG_LEVEL_ERROR = 0, LOG_LEVEL_WARN, LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG, LOG_LEVEL_TRACE
} log_level_e;
void log_output(log_level_e level, const char *module, const char *fmt, ...)
{
    (void)level; (void)module; (void)fmt;
}
void log_output_hex(log_level_e level, const char *module, const char *title,
                    const uint8_t *buf, uint16_t len)
{
    (void)level; (void)module; (void)title; (void)buf; (void)len;
}

/* ---- 桩: bsp_flash(内存镜像, 可注入"无有效数据") ---- */
static uint8_t  s_flash[128];
static uint16_t s_flash_len = 0;
static int      s_flash_valid = 0;

int bsp_flash_save(const void *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U) || (len > sizeof(s_flash))) { return -1; }
    memcpy(s_flash, data, len);
    s_flash_len   = len;
    s_flash_valid = 1;
    return 0;
}

int bsp_flash_load(void *data, uint16_t len)
{
    if (!s_flash_valid || (s_flash_len != len)) { return -1; }
    memcpy(data, s_flash, len);
    return 0;
}

/* ---- 测试脚手架 ---- */
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

static param_profile_t make_profile(uint8_t base, int16_t cx, int16_t cy)
{
    param_profile_t p;

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
    CHECK_EQ(sizeof(flash_store_t),   PARAM_PROFILE_NUM * 10, "sizeof flash_store_t");
    CHECK_EQ(PARAM_PROFILE_NUM, 3, "profile num");
}

static void test_three_sets_isolated(void)
{
    param_profile_t p0 = make_profile(10,  -10,  -20);
    param_profile_t p1 = make_profile(40,   30,   40);
    param_profile_t p2 = make_profile(70, -100, -200);

    s_flash_valid = 0;
    param_init();

    CHECK_EQ(param_profile_set(0, &p0), 0, "set p0");
    CHECK_EQ(param_profile_set(1, &p1), 0, "set p1");
    CHECK_EQ(param_profile_set(2, &p2), 0, "set p2");

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
    param_profile_t p0 = make_profile(10,  -10,  -20);
    param_profile_t p1 = make_profile(40,   30,   40);

    s_flash_valid = 0;
    param_init();

    param_profile_set(0, &p0);
    param_profile_set(1, &p1);

    /* 自主生效套 */
    param_active_set(1);
    CHECK_EQ(param_active(), 1, "active 1");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "not forced");
    CHECK_EQ(param_radc_get().x1, 41, "radc follows active");
    CHECK_EQ(param_comp()->x, 30, "comp follows active");

    /* 强制优先 */
    CHECK_EQ(param_force_set(0), 0, "force 0");
    CHECK_EQ(param_forced(), 0, "forced 0");
    CHECK_EQ(param_radc_get().x1, 11, "radc follows forced");

    /* 强制期间改自主套不生效 */
    param_active_set(1);
    CHECK_EQ(param_radc_get().x1, 11, "forced still wins");

    /* 解除强制回自主 */
    CHECK_EQ(param_force_clear(), 0, "clear");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "cleared");
    CHECK_EQ(param_radc_get().x1, 41, "back to active");

    /* 越界强制拒收 */
    CHECK_EQ(param_force_set(PARAM_PROFILE_NUM), -1, "force idx too big");
    CHECK_EQ(param_forced(), PARAM_PROFILE_NONE, "still none");
}

static void test_write_goes_to_effective_set(void)
{
    param_profile_t p0 = make_profile(10, 0, 0);
    param_profile_t p1 = make_profile(40, 0, 0);
    radc_value_t r;

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

static void test_save_and_load(void)
{
    param_profile_t p0 = make_profile(10,  -10,  -20);
    param_profile_t p2 = make_profile(70, -100, -200);

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
}

int main(void)
{
    test_size();
    test_three_sets_isolated();
    test_comp_range_and_default();
    test_active_and_force();
    test_write_goes_to_effective_set();
    test_save_and_load();

    printf("param: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
```

**注意**：测试里 `PARAM_DEF_X1` / `PARAM_DEF_COMP` 是 `param.c` 的**私有**宏，测试文件看不到它们，故断言中直接写死字面量 `40` / `-80` 并注明出处（见 Step 1 测试代码）。

> `make_profile()` 中六个字段逐个显式赋值，不使用循环——`radc` 的字段各自独立，循环写法不成立。

- [ ] **Step 2: 运行测试确认失败**

Run: `sh tests/param/run.sh`
Expected: 编译失败，报 `PARAM_PROFILE_NUM` 未定义 / `param_profile_set` 未定义

- [ ] **Step 3: 实现 — `param.h`**

把 `I301_code/App/param.h` 中的 `flash_store_t` 替换为三套模型，并追加接口声明：

```c
/* 一套配置(码值 + 补偿值, 10B)
 * 字节布局须与协议层 ls_profile_t 一致(spec §4.2) */
typedef struct
{
    radc_value_t  radc;           /* 电位器码值 6 路 */
    comp_value_t  comp;           /* 补偿值 X/Y    */
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
```

接口区追加：

```c
/* ---- 三套配置接口 ---- */
const param_profile_t *param_profile(uint8_t idx);   /* 只读快照; 越界返回 NULL */
int  param_profile_set(uint8_t idx, const param_profile_t *p); /* 整包写; 0 成功 -1 越界/空指针 */
int  param_force_set(uint8_t idx);                   /* 强制到该套; 0 成功 -1 越界 */
int  param_force_clear(void);                        /* 解除强制; 恒 0 */
uint8_t param_active(void);                          /* 当前自主生效套 */
uint8_t param_forced(void);                          /* 强制套; PARAM_PROFILE_NONE=未强制 */
void param_active_set(uint8_t idx);                  /* 由工况判定模块调用; 越界忽略 */
```

同时更新文件头注释，说明三套模型与"生效套 = 强制优先"的语义。

- [ ] **Step 4: 实现 — `param.c`**

把私有实体与相关函数替换为：

```c
/* 三套配置存储(flash 镜像); 写方收敛于本文件 */
static flash_store_t s_store;

/* 当前自主生效套(工况判定模块经 param_active_set 写入) */
static volatile uint8_t s_active;

/* 强制套: PARAM_PROFILE_NONE = 未强制(易失态, 不落 flash) */
static uint8_t s_forced;

/* 实际生效套 = 强制优先于自主 */
static uint8_t s_eff;

/* 生效套的 comp 镜像: ISR 每拍经 param_comp 只读, 零开销 */
static volatile comp_value_t s_comp;
```

`param_active()` / `param_forced()`：

```c
uint8_t param_active(void)
{
    return s_active;
}

uint8_t param_forced(void)
{
    return s_forced;
}
```

`param_profile()` / `param_profile_set()`：

```c
const param_profile_t *param_profile(uint8_t idx)
{
    if (idx >= PARAM_PROFILE_NUM)
    {
        LOG_SYS_ERROR("param profile idx out of range: %u", idx);
        return NULL;
    }
    return &s_store.sets[idx];
}

int param_profile_set(uint8_t idx, const param_profile_t *p)
{
    if ((p == NULL) || (idx >= PARAM_PROFILE_NUM))
    {
        LOG_SYS_ERROR("param profile set invalid: idx=%u", idx);
        return -1;
    }

    s_store.sets[idx] = *p;

    /* comp 一致性检查: 越界整体回退默认(规范 10-2) */
    if ((s_store.sets[idx].comp.x < COMP_VALUE_MIN) ||
        (s_store.sets[idx].comp.x > COMP_VALUE_MAX) ||
        (s_store.sets[idx].comp.y < COMP_VALUE_MIN) ||
        (s_store.sets[idx].comp.y > COMP_VALUE_MAX))
    {
        s_store.sets[idx].comp.x = PARAM_DEF_COMP;
        s_store.sets[idx].comp.y = PARAM_DEF_COMP;
        LOG_SYS_ERROR("param profile comp out of range, fallback default");
    }

    /* 若写的正是生效套, 同步应用 */
    if (idx == s_eff)
    {
        param_apply(idx);
    }
    return 0;
}
```

`param_apply()`（新增私有函数，放在 `param_comp()` 之前）：

```c
/* 把第 idx 套应用到硬件与 ISR 镜像 */
static void param_apply(uint8_t idx)
{
    s_eff     = idx;
    s_comp.x  = s_store.sets[idx].comp.x;
    s_comp.y  = s_store.sets[idx].comp.y;
    ad5290_set_all_code((const uint8_t *)&s_store.sets[idx].radc);
}
```

`param_active_set()` / `param_force_set()` / `param_force_clear()`：

```c
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

int param_force_clear(void)
{
    s_forced = PARAM_PROFILE_NONE;
    param_apply(s_active);
    return 0;
}
```

既有接口改为作用于**生效套**：

```c
radc_value_t param_radc_get(void)
{
    return s_store.sets[s_eff].radc;
}

void param_radc_set(const radc_value_t *r)
{
    if (r == NULL)
    {
        return;
    }
    s_store.sets[s_eff].radc = *r;
}

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
```

`param_save()`：

```c
int param_save(void)
{
    return bsp_flash_save(&s_store, sizeof(s_store));
}
```

`param_init()`：

```c
/* 参数初始化: flash 加载(magic+CRC+长度已由 bsp_flash 校验), 失败回退默认;
 * 加载后逐套做 comp 范围一致性检查, 越界回退默认; 最后应用第 0 套。 */
void param_init(void)
{
    uint8_t i;

    if (bsp_flash_load(&s_store, sizeof(s_store)) == 0)
    {
        for (i = 0U; i < PARAM_PROFILE_NUM; i++)
        {
            if ((s_store.sets[i].comp.x < COMP_VALUE_MIN) ||
                (s_store.sets[i].comp.x > COMP_VALUE_MAX) ||
                (s_store.sets[i].comp.y < COMP_VALUE_MIN) ||
                (s_store.sets[i].comp.y > COMP_VALUE_MAX))
            {
                s_store.sets[i].comp.x = PARAM_DEF_COMP;
                s_store.sets[i].comp.y = PARAM_DEF_COMP;
                LOG_SYS_ERROR("param comp out of range, fallback default");
            }
        }
        LOG_SYS_INFO("load param from flash");
    }
    else
    {
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
```

同时删除原有的 `comp_set_checked()`（其职责已并入 `param_init` 的逐套检查与 `param_profile_set`）。

- [ ] **Step 5: 运行测试确认通过**

Run: `sh tests/param/run.sh`
Expected: `param: N passed, 0 failed`

- [ ] **Step 6: 提交（须先获用户批准）**

```bash
git add I301_code/App/param.h I301_code/App/param.c tests/param/run.sh tests/param/test_param.c
git commit -m "参数层由单套扩为三套配置: 生效套/强制套索引与整包读写接口"
```

---

## Task 6: `ls_proto_device_app.c` 实现四个新回调

本 Task **无宿主单测**：该文件依赖 `port_trans` / `port_sys` / `ad5290` / `param` / `mylog` 一大串平台符号，桩成本高于收益。其正确性由 Step 3 的固件编译 + Step 4 的上板验证覆盖。

**Files:**
- Modify: `I301_code/App/ls_proto_device_app.c`

**Interfaces:**
- Consumes: Task 2 的协议类型；Task 4 的回调成员名；Task 5 的 `param_*` 接口
- Produces: 无（终端胶水层）

- [ ] **Step 1: 实现四个回调**

在 `I301_code/App/ls_proto_device_app.c` 中，于 `app_ctrl_save_param()` 之后追加：

```c
/* 整包写配置回调: 协议字段映射到 param 三套接口
 * 返回: 0 成功, -1 参数非法 */
static int app_ctrl_set_profile(const ls_ctrl_set_profile_t *msg)
{
    param_profile_t p;

    if (msg == NULL)
    {
        return -1;
    }

    /* 协议侧 radc[6] 顺序 = X1 X2 X3 Y1 Y2 Y3, 与 param_profile_t 一致 */
    p.radc.x1 = msg->data.radc[0];
    p.radc.x2 = msg->data.radc[1];
    p.radc.x3 = msg->data.radc[2];
    p.radc.y1 = msg->data.radc[3];
    p.radc.y2 = msg->data.radc[4];
    p.radc.y3 = msg->data.radc[5];
    p.comp.x  = msg->data.comp_x;
    p.comp.y  = msg->data.comp_y;

    /* 索引越界与 comp 范围校验均收敛于 param 层 */
    if (param_profile_set(msg->profile, &p) != 0)
    {
        return -1;
    }

    LOG_SYS_INFO("ls - set profile %u: %03u %03u %03u %03u %03u %03u comp %d %d",
                 msg->profile,
                 p.radc.x1, p.radc.x2, p.radc.x3,
                 p.radc.y1, p.radc.y2, p.radc.y3,
                 p.comp.x, p.comp.y);
    return 0;
}

/* 全量回读请求回调: 本设计无额外副作用(数据由 trans 层经
 * get_device_info_all 回调取), 恒成功 */
static int app_ctrl_get_all(void)
{
    LOG_SYS_INFO("ls - get all.");
    return 0;
}

/* 强制套回调: profile == LS_PROFILE_NONE 表示解除强制(spec §6.1)
 * 返回: 0 成功, -1 参数非法 */
static int app_ctrl_force_profile(const ls_ctrl_force_t *msg)
{
    if (msg == NULL)
    {
        return -1;
    }

    if (msg->profile == LS_PROFILE_NONE)
    {
        (void)param_force_clear();
        LOG_SYS_INFO("ls - force released, back to auto");
        return 0;
    }

    if (param_force_set(msg->profile) != 0)
    {
        return -1;
    }
    LOG_SYS_INFO("ls - force profile %u", msg->profile);
    return 0;
}
```

**注意**：上面 `app_ctrl_set_profile()` 里的 `uint8_t i;` 与 `(void)i;` 是多余的，**直接删掉这两行**，只保留其余内容。

在 `app_get_device_info()` 之后追加：

```c
/* 全量回复数据源: 三套配置 + 当前工况 */
static void app_get_device_info_all(ls_all_reply_t *reply)
{
    const param_profile_t *p;
    uint8_t i;

    if (reply == NULL)
    {
        return;
    }

    reply->device_id      = 0x1234;
    reply->device_version = 0x5678;
    reply->active_profile = param_active();
    reply->forced_profile = param_forced();

    for (i = 0U; i < LS_PROFILE_NUM; i++)
    {
        p = param_profile(i);
        if (p == NULL)
        {
            continue;
        }
        reply->profiles[i].radc[0] = p->radc.x1;
        reply->profiles[i].radc[1] = p->radc.x2;
        reply->profiles[i].radc[2] = p->radc.x3;
        reply->profiles[i].radc[3] = p->radc.y1;
        reply->profiles[i].radc[4] = p->radc.y2;
        reply->profiles[i].radc[5] = p->radc.y3;
        reply->profiles[i].comp_x  = p->comp.x;
        reply->profiles[i].comp_y  = p->comp.y;
    }
}
```

- [ ] **Step 2: 注册新回调**

把 `ls_app_init()` 中的两个回调集合替换为：

```c
void ls_app_init(void)
{
    static const ls_receive_callbacks_t r_cbs = {
        .reset_device       = app_reset_device,
        .ctrl_rdac          = app_ctrl_rdac,
        .ctrl_set_comp      = app_ctrl_set_comp,
        .ctrl_save_param    = app_ctrl_save_param,
        .ctrl_set_profile   = app_ctrl_set_profile,
        .ctrl_get_all       = app_ctrl_get_all,
        .ctrl_force_profile = app_ctrl_force_profile,
    };

    static const ls_trans_callbacks_t t_cbs = {
        .send                = app_send_data,
        .get_device_info     = app_get_device_info,
        .get_device_info_all = app_get_device_info_all,
    };

    ls_receiver_init_callbacks(&r_cbs);
    ls_trans_init_callbacks(&t_cbs);
    LOG_LSNET_INFO("ls - callbacks initialized.");
}
```

- [ ] **Step 3: 固件编译（用户执行）**

本项目固件用 Keil MDK 构建，AI 不得自行进入编译环节（项目 AGENTS.md）。

交给用户的动作：在 `I301_code/MDK-ARM/` 打开工程并全量重编译。

**期望结果**：0 error、0 warning（规范 11-1）。

**若报错，常见两类**：
- `LS_PROFILE_NUM` / `ls_profile_t` 未定义 → 检查 Task 2 的结构体是否落在 `#pragma pack(1)` 区块**内**
- `param_active_set` 未声明 → 检查 Task 5 是否把该声明加进了 `param.h`

- [ ] **Step 4: 上板验证（用户执行，判据见 spec §7.2）**

按 spec §7.2 的 8 项判据逐条验证。本计划交付物对应的判据：

| # | 步骤 | 判据 |
|---|---|---|
| 1 | 上电（flash 为旧 10 字节布局） | 串口日志出现 `load param from default`（一次性回退默认，符合 spec §3.3） |
| 2 | 重新上电 | 日志回到 `load param from flash`（说明新 30 字节布局已存住） |
| 3 | 保存三套后断电重启 | 三套值均保留 |
| 4 | 写入某套，使设备切到该套 | 硬件（电位器码值）随之改变 |

- [ ] **Step 5: 提交（须先获用户批准）**

```bash
git add I301_code/App/ls_proto_device_app.c
git commit -m "设备侧胶水实现三套配置回调: 整包写/全量回读/强制套"
```

---

## Task 7: 强制套超时自解除（spec §6.1 红线）

上位机崩溃或拔线时不会发出解除帧。若设备不主动超时，板子会被**永久钉在一套配置上且现场无法察觉**。这是 spec 里唯一标红的安全项，**上板前必须落地**。

**Files:**
- Modify: `I301_code/App/ls_proto_device_app.h`
- Modify: `I301_code/App/ls_proto_device_app.c`
- Modify: `I301_code/Core/Src/main.c:146`

**Interfaces:**
- Consumes: Task 5 的 `param_forced()`（返回 `PARAM_PROFILE_NONE` = 未强制）、`param_force_clear()`；`port_tick_ms()`（`Port/port_tick.h`，单调毫秒时基，允许回绕）；现有 `ls_app_poll()`
- Produces: `void ls_app_force_watchdog_poll(void)`（供 main 主循环调用）

- [ ] **Step 1: 头文件声明**

在 `I301_code/App/ls_proto_device_app.h` 中，于 `ls_app_poll()` 声明之后追加：

```c
/* 强制套看门狗: 主循环周期调用; 超过 LS_FORCE_TIMEOUT_MS 未收到
 * 任何上位机整帧则自动解除强制(spec §6.1) */
void ls_app_force_watchdog_poll(void);
```

同时把文件头注释的职责行补上该函数：

```c
/* ls_proto_device_app.h —— LIGHTSPACE-XY 协议设备侧胶水(APP 层)
 * 职责: 协议回调注册(电位器/补偿/保存/复位/设备信息/三套配置)、传输
 *   收发接线(发送经 port_trans, 接收成帧在 ls_app_poll)、强制套看门狗。
 * 2026-09-02 重构: 自 Proto/ 迁入 App/(协议核心保持硬件无关可复用于
 *   上位机, 设备侧胶水上移); 去 main.h/usbd 直连。
 * 2026-09-15: 新增三套配置回调与强制套超时自解除(spec §6.1)。 */
```

- [ ] **Step 2: 实现 — 时戳与看门狗**

在 `I301_code/App/ls_proto_device_app.c` 的 include 区追加 `port_tick.h`：

```c
#include "port_tick.h"       /* port_tick_ms: 强制套看门狗时基 */
```

在 `app_send_data()` 之前追加超时常量与私有状态：

```c
/* 强制套超时: 超过该时长未收到任何上位机整帧即自动解除(spec §6.1)
 * 暂定 5s, 待上板标定(spec §8 O3) */
#define LS_FORCE_TIMEOUT_MS   5000U

/* 最近一次收到上位机整帧的时基(主循环域写, 主循环域读) */
static uint32_t s_last_host_ms;
```

把 `ls_app_poll()` 中的成帧分支改为打时戳：

```c
    flen = (uint16_t)(((uint16_t)buf[13] << 8) | buf[14]);
    if (len == flen)
    {
        ls_parse(&ls_device_pkt, (uint8_t *)buf, flen);
        port_trans_rx_consume(flen);

        /* 收到任何整帧都视为上位机在线, 刷新强制套看门狗 */
        s_last_host_ms = port_tick_ms();
    }
```

在 `ls_app_poll()` 之后追加看门狗实现：

```c
/* 强制套看门狗: 未强制则空转; 已强制且超时则解除回自主。
 * 时基比较用无符号差值, 对 port_tick_ms 的 32 位回绕安全。 */
void ls_app_force_watchdog_poll(void)
{
    if (param_forced() == PARAM_PROFILE_NONE)
    {
        return;
    }

    if ((uint32_t)(port_tick_ms() - s_last_host_ms) > LS_FORCE_TIMEOUT_MS)
    {
        (void)param_force_clear();
        LOG_SYS_ERROR("force profile timeout, released to auto");
    }
}
```

- [ ] **Step 3: 主循环挂钩**

`I301_code/Core/Src/main.c` 第 146 行位于 `/* USER CODE BEGIN WHILE */`（142 行）与 `/* USER CODE END WHILE */`（147 行）之间，属**用户代码区**（非 CubeMX 生成区），可安全修改（规范 1-8）。

把 `while (1)` 循环体改为：

```c
  while (1)
  {
    ls_app_poll();   /* 传输接收成帧 + 协议分发(经 port_trans 契约) */
    ls_app_force_watchdog_poll();   /* 强制套超时自解除(spec §6.1) */
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
```

- [ ] **Step 4: 固件编译（用户执行）**

同 Task 6 Step 3：在 `I301_code/MDK-ARM/` 全量重编译，期望 0 error 0 warning。

- [ ] **Step 5: 上板验证（用户执行）**

| # | 步骤 | 判据 |
|---|---|---|
| 1 | 上位机强制到第 2 套 | 串口日志出现 `ls - force profile 2` |
| 2 | 保持强制、持续不掉线 | **不得**出现 `force profile timeout`（保活帧在刷新时戳） |
| 3 | 保持强制，拔掉 USB，等 5s | 串口日志出现 `force profile timeout, released to auto` |
| 4 | 拔线后重新上电 | `param_init` 日志显示 `forced=0xFF`（强制态不持久化，spec §6.2） |

- [ ] **Step 6: 提交（须先获用户批准）**

```bash
git add I301_code/App/ls_proto_device_app.h I301_code/App/ls_proto_device_app.c I301_code/Core/Src/main.c
git commit -m "新增强制套超时自解除: 上位机掉线 5s 后自动回到自主判定"
```

---

## 完成判据

- [ ] `sh tests/ls_proto/run.sh` 通过，0 failed
- [ ] `sh tests/param/run.sh` 通过，0 failed
- [ ] 固件全量重编译 0 error 0 warning（用户执行）
- [ ] spec §7.2 的 8 项上板判据通过（用户执行）
- [ ] **强制套超时自解除上板验证通过**（Task 7 Step 5 的 4 项：保活不误解除、拔线 5s 后自解除、重启不残留强制）
- [ ] 上位机仓库的 `Proto/` 副本**尚未同步** —— 这是后续「上位机计划」的首个 Task，**不要在本计划内顺手改**

## 已知边界

- **工况判定模块不存在**：`param_active_set()` 已就位但无人调用，`s_active` 恒为 0。按 spec §1.6，判定逻辑（怎么从 IN 通道认 30k/40k、过流降速何时触发）须另出 spec。在它落地前，**三套里只有第 0 套会按自主生效**，第 1/2 套只能通过强制套命令切换。
- **`BSP_FLASH_DATA_MAX` 保持 32**：30 字节刚好通过 `param_flash_fit_check`，无余量（spec §8 O1）。
- **旧设备升级后需重新标定**：长度为 10 字节的旧 flash 布局会被判为无效并一次性回退默认（spec §3.3），三套均为出厂默认值。

## 遗留项（须另出计划或并入本计划后续任务）

| # | 项 | 说明 |
|---|---|---|
| 1 | 工况判定模块 | spec §8 O4/O5，须另出 spec |
| 2 | 上位机 `Proto/` 副本同步 | 后续「上位机计划」首个 Task |
| 3 | `BSP_FLASH_DATA_MAX` 32→64 | spec §8 O1，当前不阻塞 |
| 4 | 强制超时时长标定 | 暂定 5s（spec §8 O3），上板调定后回填 |