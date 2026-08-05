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

## 构建

正式构建链：**VS Code + EIDE 插件**（工程配置 `I301_code/MDK-ARM/.eide/eide.yml`）。

1. VS Code 安装 EIDE 插件，打开仓库根目录；
2. 选择 target `I301_code`（AC6 编译器，预定义 `USE_HAL_DRIVER,STM32G474xx`）；
3. 构建 / 下载（ST-Link，SWD，地址 0x08000000）。

> ⚠️ `I301_code/MDK-ARM/I301_code.uvprojx` 是历史遗留的**残缺 Keil 工程**
> （缺少 task.c、mylog.c、ad5290.c、bsp_flash.c、USB/、Proto/ 等全部源文件与
> include 路径），直接用 Keil 打开必然构建失败。如需 UV4 命令行构建，可参照
> `docs/superpowers/plans/2026-08-05-ad-da-processing-path.md` 记录的方法：
> 临时按 eide.yml 补全文件清单与 include 路径（`../USB/inc;../Proto/Inc`），
> 编译器版本 V6.22→V6.23，构建后还原。

## 目录结构

```
I301_code/
  Core/             主程序与外设驱动（main/task/adc/dac/tim/opamp/ad5290/bsp_flash/mylog）
  Proto/            LS-XY 通信协议（与硬件无关，可复用）
  USB/              USB Device Bulk 收发（usbd_bulk 为业务收发入口）
  Drivers/          HAL / CMSIS（CubeMX 生成，勿手改）
  MDK-ARM/          EIDE 工程配置（.eide/）与构建产物
doc/                协议文档、原理图分析、测试数据（中文）
docs/superpowers/   设计 spec 与实施计划（含 AD-DA 处理通路）
```

## 文档索引

- 通信协议：`doc/协议/LS-XY协议文档.md`
- AD-DA 处理通路设计规格：`docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md`
- AD-DA 处理通路实施计划：`docs/superpowers/plans/2026-08-05-ad-da-processing-path.md`
- 原理图分析 / 测试数据：`doc/原理图分析/`、`doc/测试数据/`

## 近期版本

- **2026-08-05**：AD-DA 处理通路实施完成（1MHz 线性处理、预留算法槽），编译通过
  （0 Error 0 Warning），待上板验证
- 2026-07-21：仅开启数字电位器控制，走纯模拟通路
- 2026-05-29：增加 USB 通信协议与配置代码，主机识别正常
