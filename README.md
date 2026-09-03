# I301_ScanBoard — 激光振镜 XY 扫描控制板固件

产品/协议代号 **LIGHTSPACE-XY**。下位机（本仓库）以 **STM32G474RBTx** 为主控，通过
**USB Bulk** 与上位机通信，接收参数配置命令（数字电位器码值、补偿值、参数保存/查询/复位），
驱动 6 路 AD5290 数字电位器调节 X/Y 两轴模拟控制环路，并提供 1MHz 逐样本
ADC→MCU→DAC 数字处理通路（处理后波形回注模拟伺服环路的命令求和点）。

## 功能特性

- **USB Bulk 通信**：`Proto/` 协议层与硬件解耦（可复用到上位机），回调注册驱动业务逻辑；
  命令字 `0x0101` 查询 / `0x0102` 应答 / `0x0103` 复位；`0x0301` 控制应答 /
  `0x0302` 数字电位器 / `0x0303` 补偿值 / `0x0304` 参数保存。协议大端，
  固定头 `"LIGHTSPACE-XY"`(13B)。
- **数字电位器控制**：6 路 AD5290（X/Y 各 3 路，10K/100K），GPIO 模拟 SPI、共用
  CS(PC6) 并行移位；参数持久化到 Flash 最后一页（magic `"LSPY"` + CRC-16），
  上电自动恢复。
- **AD-DA 处理通路（1MHz）**：4 路 ADC（TIM3 TRGO 同步触发）采样外部波形 →
  逐样本 `y = (4095 − x) + off` 线性处理（饱和钳位 0..4095）→ 4 路 DAC 重建回注；
  双循环 DMA 乒乓（每块 64 样本、半块 32 样本中断节拍），固定延迟 ≈65µs 无抖动；
  **预留算法槽** `ad_da_process_fn`（默认 `ad_da_process_linear`）。
- **补偿值 off**：复用协议 `0x0303`（X 轴两路取 comp.x、Y 轴两路取 comp.y），
  范围 ±2000，可落盘（`0x0304`）；偏置叠加，非校准（增益/零点校准走 `0x0302`）。

## 硬件要点

- 主控：STM32G474RBTx（Cortex-M4 @170MHz）
- 采样：ADC3(PB13)=vx、ADC2(PA0)=ix、ADC4(PB15)=vy、ADC5(PA9)=iy，12bit @1MHz
- 重建：DAC1_CH1→PA4(DA_INX)、DAC1_CH2→PA5(DA_FBX)、DAC4_CH1→PB12(DA_INY，
  经 OPAMP4 跟随)、DAC4_CH2→PA8(DA_FBY，经 OPAMP5 跟随)
- 数字电位器：AD5290 ×6（X/Y 各 3 路）

> ⚠️ **使用约束**
>
> 1. 启用 AD-DA 处理通路时 **JP3 必须断开**（或外部 IN± 不接），否则外部指令与
>    DAC 重建同相叠加 ×2（spec §3.2）。
> 2. 通路运行中**避免下发 `0x0304` 参数保存**：整页 Flash 擦写（ms 级）期间 CPU
>    取指挂起、全部 ISR 停摆，DAC 会重放最后一块 64 样本后跳变到当前波形
>    （spec §9.5，知情容忍或先停通路）。
> 3. **不得再启用 TIM2 中断**（`task.c` 中 TIM2 死代码为历史参考）：其回调直写
>    DHR12R1，会与 TX DMA 竞争同一寄存器（spec §8）。
> 4. **PB3/PB4（CH-FBX/CH-FBY，.ioc 未登记）是 74HC4053 反馈出口选择脚，严禁悬空**：
>    固件默认驱动低电平 = 两轴选 X0/Y0 = driver 位置反馈直通 JB3/JB4（与原仓库行为
>    一致）；拉高 = MCU 重建（DA_FBX/DA_FBY）切出至 JB3/JB4（外部环回预留，暂未启用）。
>    悬空会导致反馈出口选通状态不定、JB3/JB4 波形异常（spec §3.1、§9.8）。

## 构建

正式构建链：**VS Code + EIDE 插件**（工程配置 `I301_code/MDK-ARM/.eide/eide.yml`）。

1. VS Code 安装 EIDE 插件，打开仓库根目录；
2. 选择 target `I301_code`（AC6 编译器，预定义 `USE_HAL_DRIVER,STM32G474xx`）；
3. 构建 / 下载（ST-Link，SWD，地址 0x08000000）。

> ⚠️ `I301_code/MDK-ARM/I301_code.uvprojx` 是历史遗留的**残缺 Keil 工程**
> （缺少 task.c、mylog.c、ad5290.c、bsp_flash.c、USB/、Proto/ 等全部源文件与
> include 路径），直接用 Keil 打开必然构建失败。如需 UV4 命令行构建，可参照
> `docs/superpowers/plans/2026-08-05-ad-da-processing-path.md` 记录的方法：
> 临时按 eide.yml 补全文件清单与 include 路径（`../USB/Inc;../Proto/Inc;../App;../Bsp`），
> 编译器版本 V6.22→V6.23，构建后还原。

## 目录结构

四层可移植架构（2026-09-02，见 `docs/superpowers/specs/2026-09-02-portable-layered-architecture-design.md`）：

```
I301_code/
  App/              APP 层：线性算法 ad_da_alg / 参数 param / 过流 ocd、ocd_sig /
                    协议设备胶水 ls_proto_device_app（禁厂商头）
  Proto/            APP 层：LS-XY 通信协议核心（与硬件无关，可复用于上位机）
  Bsp/              DRV 层：ad5290 / bsp_flash / mylog（只依赖 Port 契约，禁厂商头）
  Port/             PORT 层契约头：port_tick/gpio/flash/console/pipe/sig/trans/sys
  Platform/
    stm32g4/        PORT 实现（G4 专有；换芯片新建同级目录，含 chip_cfg.h）
  Board/            板型配置头：i301_ad_da.h（引脚 ID / 通道数 / 功能组合）
  Core/             厂商层：CubeMX 生成代码（main/外设初始化/it/msp，勿手改）
  Drivers/          厂商层：HAL / CMSIS（勿手改）
  USB/              厂商层：USB Device 库（port_trans 包装）
  MDK-ARM/          EIDE 工程配置（.eide/）与构建产物
doc/                协议文档、原理图分析、测试数据（中文）
docs/superpowers/   设计 spec 与实施计划
tests/              宿主单测（WSL gcc）
tools/              分层边界检查脚本（评审门）
```

分层边界执行：`sh tools/check_layer_boundary.sh`（App/Bsp/Proto 不得出现厂商头与 HAL 符号）。

## 文档索引

- 通信协议：`doc/协议/LS-XY协议文档.md`
- AD-DA 处理通路设计规格：`docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md`
- AD-DA 处理通路实施计划：`docs/superpowers/plans/2026-08-05-ad-da-processing-path.md`
- 原理图分析 / 测试数据：`doc/原理图分析/`、`doc/测试数据/`

## 近期版本

- **2026-09-02**：四层可移植分层架构落地（面向板B 纯模拟方案与后续多芯片适配）：
  管线/信令/传输等硬件机制下沉 `Platform/stm32g4/`，8 个 PORT 契约 + 板型配置头
  建立；App/Bsp/Proto 与厂商层完全解耦（边界脚本校验通过）；线性算法宿主单测
  42 项通过；bsp_flash 泛化（存储布局变更，首次上电参数回退默认）；
  `USBD_BULK_Recv` 成帧逻辑上移 `ls_app_poll`。**待编译与上板验证**
- **2026-08-06**：AD-DA 处理通路上板验证通过并收尾：修复 FB 通道公式（原统一反相
  致静息 ≈2.2V，改 `y = x + off` 后静息 0V、有激励出波形，spec §9.7）；修复 X 轴反馈
  波形异常——4053 选择脚 PB3/PB4 漏配悬空，补配推挽默认低电平（spec §9.8）；诊断
  脚手架清理完毕（TC 重新全屏蔽、调试打印移除、波特率还原 921600）。遗留：ADC12
  时钟 SYSCLK 超 fADC 上限（与本症状无因果，另行评估，spec §9.8 尾注）
- 2026-08-05：AD-DA 处理通路实施完成（1MHz 线性处理、预留算法槽），编译通过
  （0 Error 0 Warning），待上板验证
- 2026-07-21：仅开启数字电位器控制，走纯模拟通路
- 2026-05-29：增加 USB 通信协议与配置代码，主机识别正常
