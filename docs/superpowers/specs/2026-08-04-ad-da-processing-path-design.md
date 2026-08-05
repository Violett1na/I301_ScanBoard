# AD-DA 处理通路（1MHz 线性处理、预留算法槽）设计规格

- 日期：2026-08-04
- 状态：设计已确认，待实施（代码未修改）
- 硬件：I301 板（STM32G474RBTx），14 页版 I301X 原理图（2026-07-21 导出）
- **取代：`2026-07-29-adc-dac-dma-follower-design.md`**（零 CPU 纯透传方案作废，单链实现本规格）
- 语言/风格：遵循 `AGENTS.md`/`CLAUDE.md`——中文注释、应用逻辑在 task.c、不改 CubeMX 生成区、整数运算、连续赋值纵向对齐、错误走 `Error_Handler()`

---

## 1. 背景与目标

旧固件有一段被注释的"ADC→补偿→DAC"主循环轮询代码：采样外部波形、处理后经 DAC 回注模拟通路。
经逐节点核对原理图，该旧代码的注入点（U1C 命令求和点）与采样点（外部输入级输出节点）之间
**经 DAC 路径的端到端符号为正**（`4095−adc+comp` 恰好抵消 ADC-V 调理反相），在 JP3 断开时
构成"数字化替换模拟输入链路"的正确语义。本规格将该通路工程化：**1MHz 逐样本 1:1、
确定性时序、CPU 块处理、预留算法槽**。

设计目标（用户确认）：通过 ADC 采样波形 → MCU 处理 → 处理后波形经 DAC 送模拟通路。

## 2. 需求汇总（逐项确认于 2026-08-04 商讨）

| # | 需求 | 确认值 |
|---|---|---|
| 1 | 处理内容 | 逐样本线性变换 `y = (4095 − x) + off`，饱和钳位；**预留算法槽**（函数指针） |
| 2 | off 来源 | 复用现有 0x0303 comp 协议：`comp.x`→X 轴两路，`comp.y`→Y 轴两路；协议零改动 |
| 3 | 速率 | 1MHz 顶格，ADC/DAC 同 TIM3 TRGO，逐样本 1:1、长期不漂移 |
| 4 | 通道映射 | 4 路全驱：vx→DA_INX、ix→DA_FBX、vy→DA_INY、iy→DA_FBY |
| 5 | 旧规格 | 7-29 零 CPU 透传**取代**，不保留双模式 |

## 3. 硬件基础与约束

### 3.1 信号链（14 页版原理图，净表核对）

- 采样点：ADC-V 节点 = 外部输入级（U1B）输出经 VR2 微调后节点；调理 `Vadc = 1.25 − 0.5·Vn`。
  ADC 通道：hadc3=PB13(vx)、hadc4=PB15(vy)、hadc2=PA0(ix)、hadc5=PA9(iy)。
- 注入点：DA_INX=PA4（DAC1_CH1）、DA_INY=PB12（DAC4_CH1→OPAMP4 跟随）；
  DA_FBX=PA5（DAC1_CH2）、DA_FBY=PA8（DAC4_CH2→OPAMP5 跟随）。
- **硬件事实**：DA_FBX/DA_FBY 经 74HC4053 的 X1/Y1、公共端去向 JB3/JB4 连接器脚，
  **不进入内部模拟环路**——此两路为导出/监测/外部环回用途（用户知情，维持 4 路全驱）。
- DAC 注入点 = U1C 命令求和虚地（外部指令 R62 12K、DAC R16 12K、位置反馈 R24 100K 汇入），
  即处理后波形进入模拟伺服环的命令端。

### 3.2 使用约束（写入板级文档）

**启用数字处理通路时 JP3 必须断开（或外部 IN± 不接）**，否则外部指令直通 + DAC 重建
同相叠加 ×2。

### 3.3 现成配置（仅未启动）

TIM3：PSC=2-1、ARR=85-1、TRGO=Update → 1MHz；ADC2/3/4/5：12bit、2.5 周期、T3_TRGO
硬件触发、DMA 循环（ADC3→DMA1_CH1、ADC2→DMA1_CH2、ADC5→DMA1_CH3、ADC4→DMA1_CH4）；
DAC1 双通道外部输出、DAC4 双通道片内→OPAMP4/5；输出缓冲 OFF、HFSEL=ABOVE_160MHZ。

## 4. 总体架构与数据流

```
                 TIM3 TRGO（1MHz，唯一时钟源）
                  ├───────────────┐
     采样侧（RX） ▼               ▼ 重建侧（TX）
 ADC3(vx)─DMA→ rx_buf[0][N]   tx_buf[0][N]─DMA→ DAC1_CH1 → PA4  (INX)
 ADC2(ix)─DMA→ rx_buf[1][N]   tx_buf[1][N]─DMA→ DAC1_CH2 → PA5  (FBX)
 ADC4(vy)─DMA→ rx_buf[2][N]   tx_buf[2][N]─DMA→ DAC4_CH1 → PB12 (INY)
 ADC5(iy)─DMA→ rx_buf[3][N]   tx_buf[3][N]─DMA→ DAC4_CH2 → PA8  (FBY)
                  │               ▲
                  └─ HT/TC 中断：process_block() ─
                     y = (4095 − x) + off，饱和钳位 0..4095
```

- RX：现有 4 路 ADC 循环 DMA 改指 `rx_buf[ch]`（N=64，宏 `AD_DA_BLOCK`）。
- 处理：以 hadc3 的 `HAL_ADC_ConvHalfCpltCallback/ConvCpltCallback` 为节拍，
  每次对 4 通道同下标半块（32 样本）做线性运算写入 `tx_buf` 对应半块。
- TX：4 路新建内存→DHR 循环 DMA，DAC 触发 = T3_TRGO，1MHz 消费 tx_buf。
- 延迟：固定 32–64µs（半块~一块）+ 转换/DMA <1µs，无随机抖动。
- CPU：每块 256 样本 × ~4–8 周期 ≈ 6–12µs / 64µs ⇒ 10–20%，需在板实测。

## 5. 缓冲区与并发

- `static uint16_t rx_buf[4][AD_DA_BLOCK]`、`tx_buf[4][AD_DA_BLOCK]`；半字对齐天然满足。
- **半块相位合约（无锁核心）**：RX 半块完成时刻 = TX 进入同一半块时刻（同 TRGO）⇒
  ISR 拥有完整 32µs 窗口写该半块；实测 ~6µs，余量 >5 倍。
- 退化模式：ISR 若超时，TX 重读旧值 = 输出保持，不崩、不回卷（钳位保证）。
- 共享数据：`comp_value.x/.y` 主循环写、ISR 读，对齐加载天然原子，不加锁；
  每块结束顺带把各通道最新样本抄入 `adc_value`（保留监测/上报口）。
- **算法槽**：`ad_da_process_fn` 函数指针，默认 `ad_da_process_linear()`；
  签名 `(const uint16_t *in[4], uint16_t *out[4], uint16_t n)`；off 自取 `comp_value`。
- 回调内严禁协议/flash/日志（`AGENTS.md` 中断规范的唯一例外是块处理本身——
  纯数据搬运+整数 ALU，属实时数据通路，加注释说明）。

## 6. 初始化与启动时序

`AD_DA_Init()`（task.c 重写函数体）调用位置：`main.c` USER CODE 2，
**移至 `param_init()` 之后**（顺序：ad5290_set_init → MX_USB_DEVICE_Init → lsnet_init →
param_init → AD_DA_Init），使 off 首拍生效。

函数体顺序：

1. 4 路 ADC 偏移校准 + `HAL_Delay(10)`（沿用原流程）；
2. 运行时重配 DAC 4 通道：`HAL_DAC_ConfigChannel(... DAC_TRIGGER_T3_TRGO ...)`
   （缓冲保持 OFF）——不动 .ioc/生成代码；
3. `HAL_DAC_Start` ×4、`HAL_OPAMP_Start` ×2（先使能输出端）；
4. `HAL_DAC_SetValue` 各通道 2048；`tx_buf` 整体预填 2048（防前半块垃圾值）；
5. 创建 4 路 TX DMA（空闲通道，拟 DMA1_Ch5–Ch8）：`HAL_DMA_Init`（内存→外设、循环、
   半字、请求 = `DMA_REQUEST_DAC1_CHANNEL1/2`、`DMA_REQUEST_DAC4_CHANNEL1/2`），
   `__HAL_LINKDMA(&hdac1, DMA_Handle1/2, …)`、`(&hdac4, …)`，使能 NVIC，
   并在 `stm32g4xx_it.c` **USER CODE 区**补 4 个 IRQHandler（唯一触碰生成文件处，仅限用户区）；
   随后 `HAL_DAC_Start_DMA`（循环、长度 N）启动 TX；
6. 4 路 RX `HAL_ADC_Start_DMA` 改指 `rx_buf[ch]` 启动；
7. 最后 `HAL_TIM_Base_Start(&htim3)`——唯一时钟源最后释放，收发同拍锁相。

时序要点：TX 先于 RX 启动、时钟最后释放；TX 自启动即消费预填值，首拍 TRGO（≤1µs）
起输出预填值，随后 32–64µs 内被首块处理结果接管。启动前 DAC 输出 ≤1µs 的低值阶跃，
模拟时间常数下可忽略。任一 HAL 调用失败 → `Error_Handler()`。

## 7. 参数与协议

- off = comp：0x0303 设置、0x0304 落盘、0x0102 回显，范围 ±2000，**协议零改动**。
- 刻度：1 码 ≈0.61mV（DAC 端）≈1.22mV（模拟求和点，经 U15 ×2）。
- comp 为偏置叠加，非校准；增益/零点校准仍走 AD5290（0x0302）。
- 算法槽切换的协议命令字暂不分配（YAGNI）。

## 8. 代码组织

- 全部新增逻辑在 `Core/Src/task.c`（应用逻辑归 task.c 规范）：缓冲、回调、
  `ad_da_process_linear()`、`ad_da_process_fn`、重写 `AD_DA_Init()`；
  声明进 `Core/Inc/task.h`。
- `stm32g4xx_it.c` USER CODE 区：4 个 TX DMA IRQHandler。
- 不动：`.ioc`、`adc.c`、`dac.c`、`tim.c`、`opamp.c`、协议层、USB 层。
- 主循环旧注释代码与 TIM2 死代码保留作参考（与 7-29 规格一致）。

## 9. 风险与约束

1. X 轴 DAC 1MSPS 顶格（buffer OFF + HFSEL + PA4/PA5 负载 ≤10pF 为上板前检查项）；
2. CPU 10–20% 需在板确认；ISR 死线余量 >5 倍；
3. JP3 使用约束（§3.2）；
4. TX DMA 手工创建（§6 步骤 5）。

## 10. 验证

1. Keil 编译通过（用户执行或明确授权后代执行）；
2. 示波器对照 PB13 vs PA4、PB15 vs PB12：同波形、固定延迟 32–64µs、无抖动；X 轴重点；
3. comp 步进 ±100/±1000：求和点偏移 ≈1.22mV/码、方向正确；
4. 并发压测：跟随中持续 0x0101 查询 + 0x0302/0x0303 下发，应答无丢失、波形无毛刺；
5. （可选）GPIO 翻转抽测 ISR 宽度。

## 附：被否决的替代方案

| 方案 | 否决原因 |
|---|---|
| 主循环轮询（旧注释代码） | 更新率=主循环率，随 USB 抖动，1MHz 不成立 |
| 逐样本 ADC/DMA 中断 | 4M 中断/秒，CPU 打满 |
| 零 CPU DMA 直连（7-29 规格） | 无法做处理，被本需求取代 |
