# 可移植分层架构设计（四层 + 双配置轴）

| 项 | 值 |
|---|---|
| 日期 | 2026-09-02 |
| 状态 | 已批准(用户口头), 实施落地 2026-09-02; 目标固件编译与上板验证待用户执行 |
| 上游决策 | 2026-09-02 架构讨论：三层方案因"长期多芯片适配"需求升级为四层 |
| 关联 | 重构条目单（本 spec §7）、docs/软件开发规范.md（项目版 v1.0） |
| 下游 | 实施计划（审核通过后另出）、板B（纯模拟）方案、国民技术选型落地 |

## 1 背景与目标

### 1.1 现状动机

- I301 现有两块板并行：**板A**（AD-DA 数字处理通路，即本仓库现状）与**板B**
  （纯模拟通路：差分运放直出，DAC 全砍，ADC 只留过流检测，4053 反馈选择与
  PWM 过流信令保留）。两板为两个独立产品，固件共享协议/参数/过流决策层。
- 主控 STM32G474RBTx 存在更换可能，候选包括国民技术（N32G45x / N32H48x 等）。
- 用户明确要求：**保证固件可移植性，后期可能适配多芯片**（非一次性换芯）。

### 1.2 现状审查结论（2026-09-02）

已具备的解耦资产：Proto 核心纯 C 零 HAL + 回调注册；ocd_sig 纯状态机宿主可测；
算法槽函数指针链；param 实体私有 + 单写者收敛；ad5290 引脚表驱动。

耦合债（迁移拦路虎）：

1. `App/ad_da.c`（84 处 HAL 符号）：管线 BSP 混在应用层，算法与
   DMA/触发链/句柄未分离；
2. `App/ocd_sig.c`（51 处）：纯状态机之外的固件壳直接操作 G4 专有 AF/TIM/GPIO；
3. `USB/` 整套 ST 库，协议层直连 `usbd_bulk`，接收侧无抽象；
4. `Bsp/bsp_flash.c` G4 专有擦写，且 `bsp_flash.h` 反向 include `param.h`
   （层倒挂，违反规范 13-3）；
5. 头文件传染：`ad_da.h`/`mylog.h`/`ls_proto_data.h`/`ls_proto_device_app.h`
   把 `main.h`（HAL）带入上层编译。

## 2 总体架构：四层

```
┌──────────────────────────────────────────────────────┐
│ APP 层    App/ + Proto/                               │
│   param、ocd/ocd_sig 决策、线性算法、协议与设备胶水     │
│   可见: DRV 接口 + PORT 契约头; 禁见厂商类型            │
├──────────────────────────────────────────────────────┤
│ DRV 层    Bsp/                                        │
│   器件驱动与板级服务: ad5290、mylog、flash 存储逻辑      │
│   可见: PORT 契约头 + Board 配置; 禁见厂商类型           │
├──────────────────────────────────────────────────────┤
│ PORT 层   Port/(契约头) + Platform/<芯片>/(实现)        │
│   8 个原语契约: 无句柄、无 vtable、纯函数、直接链接       │
│   实现内部可任意使用厂商 HAL / 生成代码                  │
├──────────────────────────────────────────────────────┤
│ 厂商层    Core/ + Drivers/ + USB/                      │
│   CubeMX 生成物 + ST HAL/LL + ST USB 库               │
│   整体替换区，业务逻辑一行不放                           │
└──────────────────────────────────────────────────────┘
```

设计判据（规范 13-5）：单实例硬件实体不 handle 化；契约只表达语义不表达机制；
不为"将来可能"预设计接口。

### 2.1 与 Zephyr devicetree 的关系（决策留痕）

本架构借鉴 devicetree 哲学（硬件描述与代码分离、板与芯片正交、稳定 API 在上），
但不引入其机制（DTS 语法 + dtc 代码生成 + compatible 绑定）：本项目全为单实例
实体，绑定机制无收益；两块板 × 少量芯片的规模下，C 头文件是零工具成本的等价物。
另：Zephyr 上游无国民技术官方支持，整体迁移 Zephyr 与多芯片目标相悖。

## 3 配置双轴：Board × Platform

构建时选定**一个板型头 + 一个平台目录**，组合出"板 × 芯片"矩阵：

```
Board/i301_ad_da.h     板A 配置
Board/i301_analog.h    板B 配置（后续建，本 spec 只预留约定）
Platform/<芯片>/chip_cfg.h   芯片事实（主频、flash 页大小/编程粒度、ADC 位数等）
```

板型头内部分区（借 Zephyr "devicetree 描述硬件 / Kconfig 表达策略" 之分）：

```
Board/i301_ad_da.h
  ├── 硬件描述区: 引脚 ID 表、4053 挂接、电位器通道阻值、ADC 通道映射、
  │               AD_DA_CH_NUM 等板上布线事实
  └── 软件配置区: AD_DA_PIPELINE_ENABLE、OCD_SIG_ENABLE、OCD_ENABLE 等策略开关
```

原则：
- **通道数属板**（板上布线事实），**块长属平台**（ISR 预算参数，随芯片主频/
  实测重定，"勿改小"警示随实现文件走）；
- 引脚不再引用 CubeMX 标签（`X_SCL1_Pin`），板型头定义平台无关引脚 ID
  （如 `BOARD_PIN_X_SCL1`），平台实现负责 ID→物理口/脚映射；
- `.ioc`/N32Cube 工程仍承担外设初始化描述，板型头不重复该职责。

## 4 PORT 契约清单（8 实施 + 1 预留）

所有契约头放 `Port/`，命名 `port_<域>.h`；函数名 `port_<域>_<动作>`，
小写 snake（规范 3-1/3-7）。签名草图（实施计划阶段定稿）：

> §4 各节签名已于 2026-09-02 落地后按实现终版回写（契约演进留痕，10-3）。

### 4.1 port_tick.h —— 时基

```
uint32_t port_tick_ms(void);         /* 毫秒时基, 替代 HAL_GetTick */
void     port_delay_ms(uint32_t ms); /* 忙等延时, 替代 HAL_Delay */
void     port_delay_cycles(uint32_t n); /* 周期级忙等(位带时序用) */
uint32_t port_sysclk_hz(void);       /* 系统时钟频率 */
```

消费者：ocd.c 状态机、ocd_sig 释放窗、mylog 时间戳、param 电位器初始化延时、
ad5290 位带时序。G4 实现：ms 级转发 HAL；周期级走 DWT（原在 ad5290 内的
DWT 延时下沉至此，使 DRV 层完全无 CMSIS-core 依赖）。

### 4.2 port_gpio.h —— 引脚写

```
typedef uint16_t port_pin_t;             /* 板型头定义 ID 取值 */
void port_pin_write(port_pin_t pin, uint8_t level);
void port_pin_write_multi(const port_pin_t pins[], uint8_t n, uint8_t level);
```

消费者：ad5290 位带移位、信令 4053 切换。
`write_multi` 为落地新增（消费者驱动）：ad5290 六路 SCL 同步沿要求同端口
合并为单次寄存器写，保相位一致。G4 实现：ID 查表 → BSRR 直写。

### 4.3 port_flash.h —— flash 原语

```
int port_flash_erase(void);                       /* 擦参数区: 0 成功 */
int port_flash_write(const uint8_t *data, uint16_t len); /* 写参数区 */
int port_flash_read(uint8_t *out, uint16_t len);         /* 读参数区 */
```

参数区位置（末页）由平台 chip_cfg 决定；magic + CRC + 布局语义留在
DRV 层 bsp_flash（§7-2 泛化为 void* blob 存储，消除层倒挂）。
`read` 为落地新增：bsp_flash_load 不再直读物理地址，读取也经契约。

### 4.4 port_console.h —— 日志后端

```
void port_console_write(const char *buf, uint32_t len);
```

mylog 改为经此输出（不再依赖 printf 重定向的隐式约定）；G4 实现：USART1。

### 4.5 port_pipe.h —— 1MHz AD-DA 管线（仅板A）

契约语义（不泄露 DMA/触发链机制）：初始化 → 注册处理函数 → 平台按块节拍
回调处理 → 提供监测快照。类型归属本契约头：

```
#define AD_DA_CH_NUM  4U   /* ← 移入板型头(板上布线事实), 契约头 include 之 */

typedef struct { uint16_t x, y; } adc_value_fb_t;
typedef struct { uint16_t vx, vy, ix, iy; adc_value_fb_t fb; } adc_value_t;

typedef void (*ad_da_process_fn_t)(const uint16_t *in[AD_DA_CH_NUM],
                                   uint16_t       *out[AD_DA_CH_NUM],
                                   uint16_t        n);

int port_pipe_init(void);                              /* 0 成功; 失败由调用方走致命路径 */
void ad_da_process_fn_set(ad_da_process_fn_t fn);      /* 单写者: 平台实现持有槽位 */
ad_da_process_fn_t ad_da_process_fn_get(void);         /* ocd/ocd_sig 包装链读内层用 */
const volatile adc_value_t *port_pipe_snapshot(void);  /* 监测快照只读入口 */
```

约束：
- 槽位赋值只发生在主侧初始化（包装链：linear→ocd→ocd_sig，顺序不变；
  ocd/ocd_sig 包装发生在管线启动之后，窗口内主写/ISR 读共享，
  槽位变量按规范 5-2 加 volatile，承自原设计）；
- ISR 热路径零新增间接开销：块节拍直读槽位函数指针，与现状同构；
- AD_DA_BLOCK/AD_DA_HALF 移入平台实现（预算参数，"勿改小"警示注释随迁，
  引用先例 b65daf6）；
- 中断优先级（管线 0、TX 错误 1、DAC 欠载 2）为红线（规范 6-4），随平台实现走。

### 4.6 port_trans.h —— 字节流传输（落地修订版）

```
int port_trans_init(void);
int port_trans_send(const uint8_t *buf, uint32_t len); /* 0 成功, -1 忙/失败 */
void port_trans_poll(void);
uint16_t port_trans_rx_peek(const uint8_t **buf); /* 累积字节数 + 缓冲指针 */
void port_trans_rx_consume(uint16_t len);          /* 消费后累积清零 */
```

落地修订说明（对比草图的接收回调方案）：草图曾设计 `rx_cb` 回调，
落地改为 **peek/consume 字节流交付**——成帧判断（协议头/长度字段）是
协议知识，必须在 APP 层（`ls_app_poll`）；若平台实现回调推帧，等于把
协议解析边界推回了平台层。发送语义不变（忙即拒发）。G4 实现包装
`USBD_BULK_*`；协议层不再直连 `usbd_bulk.h`。

### 4.7 port_sig.h —— 过流信令硬件动作

```
void port_sig_init(void);                    /* 两路 1kHz/50% PWM 常开 +
                                                反馈通路让路(DAC/运放停止)+引脚复用,
                                                即现 ocd_sig 再配置清单①~⑤ */
void port_sig_switch_set(uint8_t axis, uint8_t on);
                                             /* 4053 选择脚: on=1 切 PWM 信令,
                                                on=0 回模拟直通; axis: 0=X 1=Y */
```

`ocd_sig_process` 包装层留在 APP：事件（TRIP/RELEASE）→ 调
`port_sig_switch_set`，不再出现 `HAL_GPIO_WritePin`/Pin 宏。

### 4.8 port_sys.h —— 系统动作

```
void port_sys_reset(void);   /* 协议 0x0103 复位; G4 实现 = __NVIC_SystemReset */
```

初始化致命错误路径：契约返回非 0 时，main（生成文件 USER CODE 区）保留
现有 `Error_Handler()` 语义，不扩散。

### 4.9 port_ocd_adc.h —— 板B 慢速过流采样（预留，本次不实施）

板B 的 ix/iy 低速采样契约，接口形态待板B 方案启动时按需求驱动定义
（纪律：第二个消费者出现才建）。

## 5 目录目标与迁移映射

新增目录：`Port/`、`Platform/stm32g4/`、`Board/`。
现有目录 `App/ Bsp/ Proto/ USB/ Core/ Drivers/` 保留（规范 14-2：动目录
同步 eide.yml 源清单 + include 路径 + README）。

| 现文件 | 目标 | 说明 |
|---|---|---|
| App/ad_da.h | App/ad_da.h（瘦身）+ Port/port_pipe.h | 类型/算法槽接口入契约；头保护 `__TASK_H__` 修正；摘 `main.h` |
| App/ad_da.c | App/ad_da_alg.c + Platform/stm32g4/port_pipe_g4.c | 线性算法纯模块（宿主可测）；管线主体下沉 |
| App/ocd.c/h | 原位 | `HAL_GetTick` → `port_tick_ms` |
| App/ocd_sig.h | 原位（纯单元，不动） | 已宿主可测 |
| App/ocd_sig.c | App/ocd_sig.c（包装壳）+ Platform/stm32g4/port_sig_g4.c | 再配置清单①~⑤随固件壳下沉平台 |
| App/param.c/h | 原位 | 摘 `main.h`；`HAL_Delay` → `port_delay_ms`；`lsnet_init` 移出（传输初始化归 main/port_trans） |
| Bsp/ad5290.c/h | 原位 | `GPIO_TypeDef*/BSRR` → port_gpio；引脚表改板型头 ID；DWT 延时保留（CMSIS-core 级，文件头声明依赖，见 §6.3） |
| Bsp/bsp_flash.c/h | 原位 | 接口泛化 `save/load(const void*, len)`；删 `#include "param.h"`（消倒挂） |
| Bsp/mylog.c/h | 原位 | 摘 `main.h`；时基 → port_tick；输出 → port_console |
| Proto/ls_proto_data.h | 原位 | 删 `#include "mylog.h"`（LS_LOG_ENABLE=0，宏空实现） |
| Proto/ls_proto_device_app.* | 移入 App/ | 设备侧胶水归 APP；`usbd_bulk` → port_trans；`main.h` → stdint |
| USB/、Core/、Drivers/ | 原位（厂商层） | 迁移时整体替换 |
| —— | Board/i301_ad_da.h（新建） | 引脚 ID 表 + 功能开关分区 |
| —— | Platform/stm32g4/chip_cfg.h（新建） | 170MHz、128KB/2KB 页、双字编程粒度等芯片事实 |

## 6 边界纪律与执行

1. **层可见性**：APP/DRV 层源文件禁 include 厂商头（`main.h`、`stm32g4*.h`、
   HAL/LL、`usbd_*`）与 CubeMX 外设头（`adc.h`/`dac.h` 等）；唯一合法持有者
   是 `Platform/<芯片>/` 与厂商层目录。
2. **契约演进**：新增契约须有现存消费者；契约签名变更按跨模块接口改动走
   设计说明 + 批准（规范 10-3）。
3. **Arch 级依赖白名单**：DRV/APP 允许使用 CMSIS-core 特性（DWT、`__USAT`、
   `__NOP`），使用处文件头注释声明"依赖 CMSIS-core，跨 Cortex-M 厂商可移植"。
   白名单之外的内核特性需批准。
   （落地实况：经条目实施后 DRV/APP 已无任何 CMSIS-core 依赖——DWT 延时
   下沉 port_tick、饱和钳位改等价 C；白名单保留供后续按需使用。）
4. **执行手段（修正）**：EIDE/Keil 的 include 路径是 target 级，无法按目录
   隔离，故**不采用 include 路径分治**；改用**边界检查脚本**：
   对 `App/ Bsp/ Proto/` grep 厂商头 include 与 HAL 符号，输出必须为空；
   每次重构项完成后运行，纳入评审门（规范 11-3）。

## 7 实施条目单（11-9 逐项授权、逐项总结、逐项审核）

风险升序。每项一个提交主题（提交需用户批准，11-10）。

| # | 条目 | 性质 | 验证 |
|---|---|---|---|
| 1 | 掐头文件传染 + 头保护修正（`ad_da.h`/`mylog.h`/`ls_proto_data.h`/`ls_proto_device_app.h` 摘 `main.h`/`mylog.h`；`__TASK_H__` 修正） | 零逻辑变化 | 静态自检（括号/残留 grep/TAB/行尾空白，11-8）+ 编译零警告 + 边界脚本 |
| 2 | bsp_flash 泛化：`save/load` 改 blob 接口，删对 `param.h` 的 include，消层倒挂 | 零逻辑变化 | 同上 + 参数保存/加载协议命令回归 |
| 3 | 建 `Port/` + `port_tick`：替换 `HAL_GetTick`（ocd/mylog/ocd_sig）与 `HAL_Delay`（param） | 近零变化 | 编译 + 边界脚本 + 上板时序抽测（用户执行） |
| 4 | `port_console`：mylog 输出后端化 | 零逻辑变化 | 编译 + 日志格式对照 |
| 5 | 建 `Board/i301_ad_da.h`（引脚 ID + 功能开关分区）+ `port_gpio`：ad5290 去 `GPIO_TypeDef` | 中 | 编译 + 边界脚本 + 上板电位器读写回归（用户执行） |
| 6 | `port_flash` 契约：bsp_flash 改经契约调擦写 | 近零变化 | 同 #2 |
| 7 | `port_sig`：ocd_sig 固件壳下沉 `Platform/stm32g4/port_sig_g4.c`，包装层改调 `port_sig_switch_set` | 结构重构 | 编译 + 边界脚本 + 上板过流信令三判据（用户执行，8-6） |
| 8 | `port_pipe`：线性算法抽 `App/ad_da_alg.c` 并补宿主单测（TDD 先行，8-1）；管线主体下沉 `Platform/stm32g4/port_pipe_g4.c`；AD_DA_BLOCK 随迁 | 结构重构（最大项） | 宿主单测 + 编译 + 上板 1MHz 通路全判据（固定延迟 ≈65µs、无抖动、半块相位，用户执行） |
| 9 | `port_trans` + `ls_proto_device_app` 移入 App/：发送/接收全经契约 | 结构重构 | 编译 + 协议全命令回归（0101/0102/0103/0301~0304） |
| 10 | `Platform/stm32g4/chip_cfg.h` + 边界检查脚本固化 + README/规范文档同步（14-2、0-7） | 收尾 | 全量编译 + 脚本空输出 + 文档评审 |
| 11 | `port_ocd_adc`（板B 启动时） | 预留 | —— |

排序说明：1~4 为低风险清理，可连续推进；5~6 引入双配置轴基础设施；
7~9 为三大结构重构，各自独立评审；10 收尾固化。任一项可单独叫停/回退。

**落地状态（2026-09-02，用户授权整体推进）**：条目 1~9 全部落地；
条目 10 的 chip_cfg/边界脚本/文档同步已落地，**目标固件编译与全部
上板验证待用户执行**。静态验证：宿主单测 42 项通过（gcc -Wall -Wextra
零警告）、平台无关文件全量语法检查通过、边界脚本空输出。
已知一次性影响：存储布局新增长度字段，重构后首次上电参数回退默认。

## 8 验证策略总纲

- 零逻辑变化项：静态自检 + 编译零警告 + 边界脚本（11-8、11-1）；
- 逻辑相关项：TDD 宿主单测先行（WSL gcc，8-1），覆盖阈值/量程/回绕边界（8-2）；
- 上板项：我出判据与步骤，用户构建、烧录、测量（0-3、8-6）；
- 每次上板前核对 eide.yml 三查：源清单、优化 ≥ level-2、EIDE 实例旧模型（11-2、9-3）。

## 9 约束与红线（继承附录 A，全部不变）

- JP3 必须断开；
- AD_DA_BLOCK=32 勿改小（先例 b65daf6）——随 #8 迁移至平台实现，警示注释同迁；
- 优化 ≥ level-2；
- OCD_SIG_ENABLE=1 / OCD_ENABLE=0 开关语义保持（先例 21f88c8）；
- 中断优先级架构红线（管线 0 / SysTick 15，规范 6-4）：契约化不触碰优先级
  与预算模型；
- 目标编译/烧录须授权；
- 提交粒度与信息纪律（11-9~11-11）。

## 10 开放问题

1. 国民技术选型结论（N32G45x vs N32H48x，DAC 通道数待确认）决定
   `Platform/` 第二个实现目录何时建、按哪个契约集适配；
2. 板B 原理图定稿后，`port_ocd_adc` 与 `Board/i301_analog.h` 细节另行设计；
3. 边界检查脚本的存放位置（`tools/` 或并入既有脚本）实施时定；
4. `ls_packet_t` 两个 5KB 全局包（ls_host_pkt/ls_device_pkt）的写方归属
   审查不在本 spec 范围，列为后续待查项。
