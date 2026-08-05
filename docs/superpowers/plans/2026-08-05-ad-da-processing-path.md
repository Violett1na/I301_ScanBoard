# AD-DA 处理通路（1MHz 线性处理、预留算法槽）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按 spec 实现 1MHz 逐样本 ADC→MCU→DAC 线性处理通路：`y=(4095−x)+off` 钳位，双循环 DMA 乒乓 + 半块中断块处理，预留算法槽。

**Architecture:** 4 路 ADC（ADC3=vx、ADC2=ix、ADC4=vy、ADC5=iy）由 TIM3 TRGO（1MHz）同步触发，循环 DMA 写入 `rx_buf[4][64]`；以 hadc3 DMA 的半传输/全传输中断为唯一节拍，对 32 样本半块做线性运算写入 `tx_buf[4][64]`；4 路手工创建的内存→DHR 循环 DMA（DMA1_Ch5–8，DMAMUX 请求挂 DAC1/DAC4）在同一 TRGO 下 1MHz 消费 tx_buf，收发同拍锁相。off 复用协议 comp 值（0x0303），协议零改动。

**Tech Stack:** STM32G474RBTx、STM32 HAL（G4 驱动已在 `I301_code/Drivers/`）、MDK-ARM (armclang/Keil)。

**Spec:** `docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md`（先读 spec 再动手）。

## Global Constraints

- **语言**：全程简体中文注释与沟通（AGENTS.md）。
- **不改 CubeMX 自动生成区域**：用户代码只写在 `/* USER CODE BEGIN/END */` 之间；`.ioc`、`adc.c`、`dac.c`、`tim.c`、`opamp.c`、`dma.c`、`Proto/`、`USB/`、`Drivers/` 一律不动。本计划唯一触碰生成文件处是 `stm32g4xx_it.c` 的 USER CODE 区（Task 4）。
- **编译/烧录约束（AGENTS.md）**：未经用户明确要求不得主动编译/烧录。计划中的构建命令由用户执行，或用户明确授权后代执行。
- **错误处理**：任一 HAL 调用失败 → `Error_Handler()`。
- **整数运算**：MCU 侧只用整数 + 缩放，不用浮点。
- **命名**：函数 `模块_功能`；宏全大写下划线；全局变量遵循现有风格（`comp_value` 式无前缀）；连续同类赋值纵向对齐。
- **ISR 规范**：回调内严禁协议/flash/日志调用；块处理本身（纯数据搬运 + 整数 ALU）是 AGENTS.md 中断规范的已确认例外，需注释说明。
- **协议零改动**：off = `comp_value`（0x0303 设置、0x0304 落盘、0x0102 回显，范围 ±2000）。
- **硬件约束**：启用本通路时 **JP3 必须断开**（或外部 IN± 不接），否则外部指令与 DAC 重建同相叠加 ×2（spec §3.2）。
- **工作区现状**：仓库当前有与本任务无关的未提交改动（`I301_code/Core/Inc/main.h`、`I301_code/Core/Src/gpio.c` 已修改，若干未跟踪文件）。所有提交一律 `git add <具体文件>`，禁止 `git add -A`/`git add .`。
- **与 spec 的一处实现细化（已在计划内标注）**：spec §6 步骤 5 要求 TX DMA "使能 NVIC + 4 个 IRQHandler"。本计划照做，但在 `HAL_DAC_Start_DMA` 之后屏蔽各 TX 通道的 HT/TC 中断、仅保留 TE（传输错误）——循环 DMA 每 64µs 置位 HT/TC，若不屏蔽会产生约 12.5 万次/秒的空中断（≈6% CPU）。NVIC 使能与 4 个 IRQHandler 均保留，语义不变，仅省去无用中断开销。

## File Structure

| 文件 | 动作 | 职责 |
|---|---|---|
| `I301_code/Core/Inc/task.h` | 修改 | AD-DA 公共声明：缓冲宏、`ad_da_process_fn_t` 算法槽类型、`ad_da_process_fn` 与 `ad_da_process_linear` 声明 |
| `I301_code/Core/Src/task.c` | 修改 | 乒乓缓冲、线性算法、算法槽指针、半块处理、ADC HT/TC 回调、TX DMA 手工创建、重写 `AD_DA_Init()` |
| `I301_code/Core/Src/stm32g4xx_it.c` | 修改（仅 USER CODE 区） | 4 个 TX DMA IRQHandler（DMA1_Ch5–8）+ extern 声明 |
| `I301_code/Core/Src/main.c` | 修改（仅 USER CODE 2） | 启动序列：`AD_DA_Init()` 移至 `param_init()` 之后 |

已核实的现状事实（写代码时直接依赖，无需重新调研）：

- DMA 占用：DMA1_Ch1=ADC3(vx)、DMA1_Ch2=ADC2(ix)、DMA1_Ch3=ADC5(iy)、DMA1_Ch4=ADC4(vy)、DMA2_Ch1=ADC1（未启用通路）。**DMA1_Ch5–8 空闲**，用作 TX。
- `I301_code/Core/Src/dma.c` 已把 DMA1_Channel1_IRQn 优先级设为 0,0，但 `HAL_NVIC_EnableIRQ` 被注释——由 `AD_DA_Init()` 运行时使能。
- HAL 事实（已对源码核实）：`HAL_DAC_ConfigChannel` 会把 `sConfig->DAC_Trigger`（含 TEN 位）写入 CR；`HAL_DAC_Start_DMA(hdac, Channel, pData, Length, Alignment)` 使用 `hdac->DMA_Handle1/DMA_Handle2`，内部经 `HAL_DMA_Start_IT` 使能 HT/TC/TE 并使能 DAC DMAUDR 中断；`HAL_DAC_SetValue` 参数顺序为 `(hdac, Channel, Alignment, Data)`；`__HAL_LINKDMA` 第一参数是**外设句柄指针**（`&hdac1`）。
- `DMA_REQUEST_DAC1_CHANNEL1/2`、`DMA_REQUEST_DAC4_CHANNEL1/2`、`DAC_TRIGGER_T3_TRGO`、`DMA1_Channel5–8_IRQn`、启动文件弱符号 `DMA1_Channel5–8_IRQHandler` 均存在。
- `HAL_ADC_Start_DMA` 会注册 HT/TC DMA 回调 → `HAL_ADC_ConvHalfCpltCallback`/`HAL_ADC_ConvCpltCallback` 可用作节拍。
- TIM3：PSC=2-1、ARR=85-1、TRGO=Update → 170MHz/2/85 = 1MHz。
- Keil 工程已包含 task.c/task.h/stm32g4xx_it.c，**无需改 .uvprojx**，无新增文件。

---

### Task 0: 提交 spec 与计划文档

**Files:**
- Commit: `docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md`（已存在、未跟踪）
- Commit: `docs/superpowers/plans/2026-08-05-ad-da-processing-path.md`（本文件）

**Interfaces:**
- Produces: spec 与计划进入版本库，后续 Task 可引用路径。

- [ ] **Step 1: 确认 docs/ 下只有本任务涉及的文件**

Run: `git status --short docs/`
Expected: 仅列出 `docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md` 与 `docs/superpowers/plans/2026-08-05-ad-da-processing-path.md`（`??` 未跟踪）。若多出其他文件，只提交这两个。

- [ ] **Step 2: 提交**

```bash
git add docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md docs/superpowers/plans/2026-08-05-ad-da-processing-path.md
git commit -m "docs: 增加AD-DA处理通路设计spec与实施计划"
```

Expected: 提交成功，2 个新文件入库。

---

### Task 1: task.h 增加 AD-DA 公共声明

**Files:**
- Modify: `I301_code/Core/Inc/task.h:78-80`（`adc_filter` 声明之后、`#endif` 之前插入）

**Interfaces:**
- Produces: `AD_DA_BLOCK`（=64U）、`AD_DA_HALF`（=32U）、`AD_DA_CH_NUM`（=4U）、`ad_da_process_fn_t`（算法槽函数指针类型）、`extern ad_da_process_fn_t ad_da_process_fn`、`void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM], uint16_t *out[AD_DA_CH_NUM], uint16_t n)` —— Task 2/3 的定义与调用均依赖这些名字与签名。

- [ ] **Step 1: 在 task.h 插入声明**

把文件末尾的：

```c
uint16_t adc_filter(uint16_t value);

#endif
```

替换为：

```c
uint16_t adc_filter(uint16_t value);

/* --------------------------------------------------------------
 * AD-DA 处理通路（1MHz 逐样本线性处理、预留算法槽）
 * spec: docs/superpowers/specs/2026-08-04-ad-da-processing-path-design.md
 * -------------------------------------------------------------- */
#define AD_DA_BLOCK   64U                       /* RX/TX 乒乓缓冲每通道样本数 */
#define AD_DA_HALF    (AD_DA_BLOCK / 2U)        /* 半块长度 = 块处理单位 */
#define AD_DA_CH_NUM  4U                        /* 通道数: 0=vx 1=ix 2=vy 3=iy */

/* 算法槽签名：一次处理 AD_DA_CH_NUM 个通道各 n 个样本
 * in: 各通道 RX 半块指针数组；out: 各通道 TX 半块指针数组 */
typedef void (*ad_da_process_fn_t)(const uint16_t *in[AD_DA_CH_NUM],
                                   uint16_t       *out[AD_DA_CH_NUM],
                                   uint16_t        n);

extern ad_da_process_fn_t ad_da_process_fn;     /* 算法槽，默认指向 ad_da_process_linear */

/* 默认线性算法：y = (4095 - x) + off，饱和钳位 0..4095 */
void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM],
                          uint16_t       *out[AD_DA_CH_NUM],
                          uint16_t        n);

#endif
```

- [ ] **Step 2: 编译验证（用户执行，或经用户明确授权后代执行）**

Run（Keil 安装路径按实际调整，常见 `C:\Keil_v5`；命令在仓库根目录执行）：

```
"C:\Keil_v5\UV4.exe" -b I301_code\MDK-ARM\I301_code.uvprojx -j0 -o build_log.txt
```

Expected: `I301_code/MDK-ARM/build_log.txt` 结尾为 `"I301_code.axf" - 0 Error(s), 0 Warning(s).`（纯声明，不产生新告警）。

- [ ] **Step 3: 提交**

```bash
git add I301_code/Core/Inc/task.h
git commit -m "task.h: 增加AD-DA处理通路公共声明(缓冲宏/算法槽类型)"
```

---

### Task 2: task.c 乒乓缓冲、线性算法、算法槽与块处理节拍

**Files:**
- Modify: `I301_code/Core/Src/task.c`（在 `comp_value_t comp_value;` 行之后、`/*  初始化函数  */` 注释行之前插入一整节；以文本锚点定位，勿依赖行号）

**Interfaces:**
- Consumes: Task 1 的 `AD_DA_BLOCK/AD_DA_HALF/AD_DA_CH_NUM/ad_da_process_fn_t`；现有全局 `comp_value`（int16_t x/y）、`adc_value`（volatile，字段 vx/vy/ix/iy）。
- Produces: `rx_buf`/`tx_buf`（static，Task 3 的 DMA 起止地址）、`ad_da_process_fn`（算法槽实体）、`HAL_ADC_ConvHalfCpltCallback`/`HAL_ADC_ConvCpltCallback`（hadc3 DMA 中断到达即生效）。

- [ ] **Step 1: 在 task.c 插入 AD-DA 数据通路一节**

在 `comp_value_t comp_value;` 之后、`/*  初始化函数  */` 注释之前插入：

```c
/* ------------------------------------------------------------------
 * AD-DA 处理通路（1MHz 逐样本线性处理、预留算法槽）
 *
 * 数据流: TIM3 TRGO(1MHz) → 4路ADC → rx_buf → 块处理 → tx_buf → 4路DAC
 *   rx_buf[0]=vx(ADC3/PB13) → tx_buf[0] → DAC1_CH1 → PA4  (DA_INX)
 *   rx_buf[1]=ix(ADC2/PA0)  → tx_buf[1] → DAC1_CH2 → PA5  (DA_FBX)
 *   rx_buf[2]=vy(ADC4/PB15) → tx_buf[2] → DAC4_CH1 → PB12 (DA_INY, 经OPAMP4跟随)
 *   rx_buf[3]=iy(ADC5/PA9)  → tx_buf[3] → DAC4_CH2 → PA8  (DA_FBY, 经OPAMP5跟随)
 *
 * 半块相位合约(无锁核心): RX 半块完成时刻 = TX 进入同一半块时刻(同 TRGO)
 * ⇒ ISR 拥有完整 32µs 窗口写该半块(实测约 6µs, 余量 >5 倍)；
 * 若超死线, TX 重读旧值 = 输出保持, 不崩(钳位保证)。
 * ------------------------------------------------------------------ */
static uint16_t rx_buf[AD_DA_CH_NUM][AD_DA_BLOCK];   /* RX: ADC 循环 DMA 写入 */
static uint16_t tx_buf[AD_DA_CH_NUM][AD_DA_BLOCK];   /* TX: DMA 循环读出至 DAC DHR */

/* 默认线性算法: y = (4095 - x) + off, 饱和钳位 0..4095
 * off 取协议补偿值: ch0/ch1(X轴)用 comp_value.x, ch2/ch3(Y轴)用 comp_value.y
 * (comp_value 主循环写、ISR 读, 对齐加载天然原子, 不加锁)
 * 刻度: 1 LSB ≈ 0.61mV(DAC端) ≈ 1.22mV(模拟求和点, 经 U15 ×2)；off 为偏置叠加, 非校准 */
void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM],
                          uint16_t       *out[AD_DA_CH_NUM],
                          uint16_t        n)
{
    int16_t off[AD_DA_CH_NUM];
    off[0] = comp_value.x;      /* vx → DA_INX */
    off[1] = comp_value.x;      /* ix → DA_FBX */
    off[2] = comp_value.y;      /* vy → DA_INY */
    off[3] = comp_value.y;      /* iy → DA_FBY */

    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        for (uint16_t i = 0; i < n; i++)
        {
            int32_t y = (int32_t)(4095U - in[ch][i]) + off[ch];
            if (y < 0)    y = 0;
            if (y > 4095) y = 4095;
            out[ch][i] = (uint16_t)y;
        }
    }
}

/* 算法槽: 运行时可换的处理函数(暂不分配协议命令字, YAGNI) */
ad_da_process_fn_t ad_da_process_fn = ad_da_process_linear;

/* 块处理: 处理自 offset 起的一个半块(全部通道) */
static void ad_da_process_half(uint16_t offset)
{
    const uint16_t *in[AD_DA_CH_NUM];
    uint16_t       *out[AD_DA_CH_NUM];

    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        in[ch]  = &rx_buf[ch][offset];
        out[ch] = &tx_buf[ch][offset];
    }
    ad_da_process_fn(in, out, AD_DA_HALF);
}

/* 块处理节拍: 仅以 hadc3(DMA1_Ch1)的 HT/TC 为全局调度点。
 * ISR 内容为纯数据搬运 + 整数 ALU(实时数据通路, AGENTS.md 中断规范
 * 的已确认例外条款), 严禁在此调用协议/flash/日志。 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC3)
    {
        ad_da_process_half(0U);                 /* 前半块 */
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC3)
    {
        ad_da_process_half(AD_DA_HALF);         /* 后半块 */

        /* 顺带更新监测值: 取本块最后一个样本(保留监测/上报口) */
        adc_value.vx = rx_buf[0][AD_DA_BLOCK - 1U];
        adc_value.ix = rx_buf[1][AD_DA_BLOCK - 1U];
        adc_value.vy = rx_buf[2][AD_DA_BLOCK - 1U];
        adc_value.iy = rx_buf[3][AD_DA_BLOCK - 1U];
    }
}
```

- [ ] **Step 2: 静态核对（无需上板）**

逐项确认：`rx_buf`/`tx_buf` 索引 0..3 与通道映射注释一致（0=vx,1=ix,2=vy,3=iy）；`adc_value` 字段名按 task.h 现有定义（vx/vy/ix/iy）逐个赋值；`4095U - in[ch][i]` 先提升到 int 再加 `off`（int16_t），无溢出（最大 4095+2000=6095 < INT32_MAX）。

- [ ] **Step 3: 编译验证（用户执行或明确授权）**

Run: 同 Task 1 Step 2 的 Keil 命令。
Expected: `0 Error(s), 0 Warning(s)`（`ad_da_process_half` 为 static 且已被回调引用，无 unused 告警）。

- [ ] **Step 4: 提交**

```bash
git add I301_code/Core/Src/task.c
git commit -m "task.c: 实现AD-DA乒乓缓冲、线性算法与半块处理节拍"
```

---

### Task 3: task.c TX DMA 手工创建与 AD_DA_Init 重写

**Files:**
- Modify: `I301_code/Core/Src/task.c`（整体替换现有 `AD_DA_Init` 函数，含其上方 `/*  初始化函数  */` 注释行；位于 Task 2 插入节之后，以文本锚点定位，勿依赖行号）

**Interfaces:**
- Consumes: Task 2 的 `rx_buf`/`tx_buf`；现有 `hadc2..hadc5`、`hdac1`、`hdac4`、`hopamp4`、`hopamp5`、`htim3` 句柄；`ADC_Offset_Calibration()`（adc.h）；`DMA_REQUEST_DAC1_CHANNEL1/2`、`DMA_REQUEST_DAC4_CHANNEL1/2`、`DAC_TRIGGER_T3_TRGO`、`DMA1_Channel5..8`/`DMA1_Channel5..8_IRQn`（HAL/CMSIS）。
- Produces: 全局 `DMA_HandleTypeDef hdma_dac1_ch1/hdma_dac1_ch2/hdma_dac4_ch1/hdma_dac4_ch2`（Task 4 在 stm32g4xx_it.c extern 引用）；重写后的 `void AD_DA_Init(void)`（Task 5 在 main.c 调用）。

- [ ] **Step 1: 整段替换 AD_DA_Init**

把现有函数（自 `/*  初始化函数  */` 注释行至函数右花括号）整体删除：

```c
/*  初始化函数  */
void AD_DA_Init(void)
{
	/*执行ADC偏移校准*/
	ADC_Offset_Calibration(&hadc1);

	ADC_Offset_Calibration(&hadc2);
	ADC_Offset_Calibration(&hadc3);
	ADC_Offset_Calibration(&hadc4);
	ADC_Offset_Calibration(&hadc5);
	HAL_Delay(10);
	/*启动DAC及对应的跟随器*/
	HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
	HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);
	HAL_DAC_Start(&hdac4, DAC_CHANNEL_1);	
	HAL_DAC_Start(&hdac4, DAC_CHANNEL_2);	
	HAL_OPAMP_Start(&hopamp4);
	HAL_OPAMP_Start(&hopamp5);
	/*设置DAC初始默认输出值*/
    DAC_INX_SET(2048);
    DAC_FBX_SET(0);	
    DAC_INY_SET(2048);
    DAC_FBY_SET(0);
	/*启动ADC的DMA传输*/
	HAL_ADC_Start_DMA(&hadc2, (uint32_t*)&adc_value.ix, 1);
	HAL_ADC_Start_DMA(&hadc3, (uint32_t*)&adc_value.vx, 1);
	HAL_ADC_Start_DMA(&hadc4, (uint32_t*)&adc_value.vy, 1);
	HAL_ADC_Start_DMA(&hadc5, (uint32_t*)&adc_value.iy, 1);

	// HAL_ADC_Start_DMA(&hadc1, (uint32_t*)&adc_value.fb, 2);
	HAL_Delay(10);
	/*启动触发ADC的定时器*/
	HAL_TIM_Base_Start(&htim3);
	// HAL_TIM_Base_Start_IT(&htim2);
}
```

替换为（启动时序 = spec §6：校准 → 重配触发 → 使能输出端 → 初值预填 → TX DMA → RX DMA → 时钟最后释放）：

```c
/* ------------------------------------------------------------------
 * AD-DA 处理通路初始化（时序见 spec §6：TX 先于 RX 启动、时钟最后释放）
 * ------------------------------------------------------------------ */

/* TX DMA 句柄(.ioc 无 DAC DMA, 手工创建), stm32g4xx_it.c 中 extern 引用 */
DMA_HandleTypeDef hdma_dac1_ch1;
DMA_HandleTypeDef hdma_dac1_ch2;
DMA_HandleTypeDef hdma_dac4_ch1;
DMA_HandleTypeDef hdma_dac4_ch2;

/* 创建 1 路 TX DMA: 内存→外设、循环、半字, DMAMUX 请求挂 DAC 通道 */
static void tx_dma_create(DMA_HandleTypeDef *hdma, DMA_Channel_TypeDef *ch, uint32_t request)
{
    hdma->Instance                 = ch;
    hdma->Init.Request             = request;
    hdma->Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma->Init.MemInc              = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma->Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma->Init.Mode                = DMA_CIRCULAR;
    hdma->Init.Priority            = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(hdma) != HAL_OK)
    {
        Error_Handler();
    }
}

/*  初始化函数  */
void AD_DA_Init(void)
{
    DAC_ChannelConfTypeDef sConfig = {0};

    /* 1. 4 路 RX ADC 偏移校准(沿用原流程) */
    ADC_Offset_Calibration(&hadc2);
    ADC_Offset_Calibration(&hadc3);
    ADC_Offset_Calibration(&hadc4);
    ADC_Offset_Calibration(&hadc5);
    HAL_Delay(10);

    /* 2. 运行时重配 DAC 触发 = TIM3 TRGO(缓冲保持 OFF, 不动 .ioc/生成代码) */
    sConfig.DAC_HighFrequency           = DAC_HIGH_FREQUENCY_INTERFACE_MODE_ABOVE_160MHZ;
    sConfig.DAC_DMADoubleDataMode       = DISABLE;
    sConfig.DAC_SignedFormat            = DISABLE;
    sConfig.DAC_SampleAndHold           = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger                 = DAC_TRIGGER_T3_TRGO;
    sConfig.DAC_Trigger2                = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer            = DAC_OUTPUTBUFFER_DISABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;   /* DAC1: 直接出脚 */
    sConfig.DAC_UserTrimming            = DAC_TRIMMING_FACTORY;
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;   /* DAC4: 片内进 OPAMP4/5 */
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_2) != HAL_OK) Error_Handler();

    /* 3. 先使能输出端 */
    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac4, DAC_CHANNEL_1)   != HAL_OK) Error_Handler();
    if (HAL_DAC_Start(&hdac4, DAC_CHANNEL_2)   != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Start(&hopamp4)              != HAL_OK) Error_Handler();
    if (HAL_OPAMP_Start(&hopamp5)              != HAL_OK) Error_Handler();

    /* 4. 初值 2048(中点) + tx_buf 整体预填(防前半块垃圾值) */
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        for (uint16_t i = 0; i < AD_DA_BLOCK; i++)
        {
            tx_buf[ch][i] = 2048;
        }
    }

    /* 5. 手工创建 4 路 TX DMA(DMA1_Ch5–Ch8), 链接到 DAC 句柄 */
    tx_dma_create(&hdma_dac1_ch1, DMA1_Channel5, DMA_REQUEST_DAC1_CHANNEL1);
    tx_dma_create(&hdma_dac1_ch2, DMA1_Channel6, DMA_REQUEST_DAC1_CHANNEL2);
    tx_dma_create(&hdma_dac4_ch1, DMA1_Channel7, DMA_REQUEST_DAC4_CHANNEL1);
    tx_dma_create(&hdma_dac4_ch2, DMA1_Channel8, DMA_REQUEST_DAC4_CHANNEL2);
    __HAL_LINKDMA(&hdac1, DMA_Handle1, hdma_dac1_ch1);
    __HAL_LINKDMA(&hdac1, DMA_Handle2, hdma_dac1_ch2);
    __HAL_LINKDMA(&hdac4, DMA_Handle1, hdma_dac4_ch1);
    __HAL_LINKDMA(&hdac4, DMA_Handle2, hdma_dac4_ch2);

    /* TX NVIC: 优先级 1(低于 RX 节拍中断 0, 避免抢占块处理), 仅处理传输错误 */
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 1, 0);
    HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 1, 0);
    HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 1, 0);
    HAL_NVIC_SetPriority(DMA1_Channel8_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel8_IRQn);

    /* 启动 4 路 TX DMA(循环、长度 AD_DA_BLOCK), 启动后即消费预填的 2048 */
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)tx_buf[0], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, (uint32_t *)tx_buf[1], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
    if (HAL_DAC_Start_DMA(&hdac4, DAC_CHANNEL_1, (uint32_t *)tx_buf[2], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
    if (HAL_DAC_Start_DMA(&hdac4, DAC_CHANNEL_2, (uint32_t *)tx_buf[3], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();

    /* 循环 DMA 每 64µs 置位 HT/TC, 本设计 TX 侧无需块中断:
       屏蔽 HT/TC、仅保留 TE(传输错误), 避免 ~12.5万次/秒 空中断(见 Global Constraints) */
    __HAL_DMA_DISABLE_IT(&hdma_dac1_ch1, DMA_IT_HT | DMA_IT_TC);
    __HAL_DMA_DISABLE_IT(&hdma_dac1_ch2, DMA_IT_HT | DMA_IT_TC);
    __HAL_DMA_DISABLE_IT(&hdma_dac4_ch1, DMA_IT_HT | DMA_IT_TC);
    __HAL_DMA_DISABLE_IT(&hdma_dac4_ch2, DMA_IT_HT | DMA_IT_TC);

    /* 6. RX DMA 改指 rx_buf 并启动; 使能 hadc3 DMA 中断(块处理节拍, 优先级已在 dma.c 设 0) */
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    if (HAL_ADC_Start_DMA(&hadc3, (uint32_t *)rx_buf[0], AD_DA_BLOCK) != HAL_OK) Error_Handler();
    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)rx_buf[1], AD_DA_BLOCK) != HAL_OK) Error_Handler();
    if (HAL_ADC_Start_DMA(&hadc4, (uint32_t *)rx_buf[2], AD_DA_BLOCK) != HAL_OK) Error_Handler();
    if (HAL_ADC_Start_DMA(&hadc5, (uint32_t *)rx_buf[3], AD_DA_BLOCK) != HAL_OK) Error_Handler();

    /* 7. 唯一时钟源最后释放: 收发同拍锁相 */
    if (HAL_TIM_Base_Start(&htim3) != HAL_OK) Error_Handler();
}
```

- [ ] **Step 2: 静态核对**

逐项确认：`__HAL_LINKDMA` 第一参数带 `&`；`HAL_DAC_SetValue` 参数序 `(hdac, Channel, Alignment, Data)`；`tx_buf[0..3]` 依次对应 DAC1_CH1/DAC1_CH2/DAC4_CH1/DAC4_CH2；`rx_buf[0..3]` 依次对应 hadc3/hadc2/hadc4/hadc5（与 Task 2 映射注释一致）；`HAL_TIM_Base_Start` 是最后一条外设启动语句。

- [ ] **Step 3: 编译验证（用户执行或明确授权）**

Run: 同 Task 1 Step 2 的 Keil 命令。
Expected: `0 Error(s), 0 Warning(s)`。此时 `AD_DA_Init` 仍无人调用（main.c 尚未改），链接正常。

- [ ] **Step 4: 提交**

```bash
git add I301_code/Core/Src/task.c
git commit -m "task.c: 手工创建TX DMA并重写AD_DA_Init(1MHz同拍启动时序)"
```

---

### Task 4: stm32g4xx_it.c USER CODE 区补 4 个 TX DMA IRQHandler

**Files:**
- Modify: `I301_code/Core/Src/stm32g4xx_it.c:66-69`（`USER CODE BEGIN EV` 区加 extern）
- Modify: `I301_code/Core/Src/stm32g4xx_it.c:293-298`（`USER CODE BEGIN 1` 区加 4 个 Handler）

**Interfaces:**
- Consumes: Task 3 定义的 `hdma_dac1_ch1/hdma_dac1_ch2/hdma_dac4_ch1/hdma_dac4_ch2`。
- Produces: `DMA1_Channel5..8_IRQHandler` 实体——Task 3 中 `HAL_NVIC_EnableIRQ(DMA1_Channel5..8_IRQn)` 生效后，TE 错误中断有落点。

- [ ] **Step 1: EV 区加 extern**

在 `stm32g4xx_it.c` 的 `/* USER CODE BEGIN EV */` 区（现有 `extern PCD_HandleTypeDef hpcd_USB_FS;` 旁）追加：

```c
extern DMA_HandleTypeDef hdma_dac1_ch1;
extern DMA_HandleTypeDef hdma_dac1_ch2;
extern DMA_HandleTypeDef hdma_dac4_ch1;
extern DMA_HandleTypeDef hdma_dac4_ch2;
```

- [ ] **Step 2: USER CODE 1 区加 4 个 Handler**

在 `/* USER CODE BEGIN 1 */` 区（现有 `USB_LP_IRQHandler` 之后）追加：

```c
/* TX DMA 中断: 循环模式 HT/TC 已在 AD_DA_Init 中屏蔽, 此处仅兜底传输错误(TE) */
void DMA1_Channel5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dac1_ch1);
}

void DMA1_Channel6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dac1_ch2);
}

void DMA1_Channel7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dac4_ch1);
}

void DMA1_Channel8_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dac4_ch2);
}
```

- [ ] **Step 3: 静态核对**

确认 4 个函数名与启动文件 `startup_stm32g474xx.s` 向量表符号完全一致（`DMA1_Channel5_IRQHandler` … `DMA1_Channel8_IRQHandler`）；确认只改了 USER CODE 区，生成区未动。

- [ ] **Step 4: 编译验证（用户执行或明确授权）**

Run: 同 Task 1 Step 2 的 Keil 命令。
Expected: `0 Error(s), 0 Warning(s)`。

- [ ] **Step 5: 提交**

```bash
git add I301_code/Core/Src/stm32g4xx_it.c
git commit -m "stm32g4xx_it.c: USER CODE区补4路TX DMA中断handler"
```

---

### Task 5: main.c 启用处理通路 + 上板验证

**Files:**
- Modify: `I301_code/Core/Src/main.c:109-116`（`USER CODE BEGIN 2` 区）

**Interfaces:**
- Consumes: Task 3 的 `AD_DA_Init()`；现有 `ad5290_set_init()`、`param_init()`、`lsnet_init()`、`MX_USB_DEVICE_Init()`。
- Produces: 上电即运行的 1MHz 处理通路固件。

- [ ] **Step 1: 调整启动序列**

把 main.c 的：

```c
  /* USER CODE BEGIN 2 */
  LOG_SYS_INFO("===================================================");
  ad5290_set_init();
  // AD_DA_Init();
  MX_USB_DEVICE_Init();
  lsnet_init();
  param_init();
  /* USER CODE END 2 */
```

替换为：

```c
  /* USER CODE BEGIN 2 */
  LOG_SYS_INFO("===================================================");
  ad5290_set_init();
  MX_USB_DEVICE_Init();
  lsnet_init();
  param_init();
  /* 启动 AD-DA 处理通路: 须在 param_init 之后, 使 comp 偏置首拍生效。
     硬件约束: JP3 必须断开(或外部 IN± 不接), 见 spec §3.2 */
  AD_DA_Init();
  /* USER CODE END 2 */
```

主循环内旧注释代码与 TIM2 回调保留不动（spec §8）。

- [ ] **Step 2: 编译验证（用户执行或明确授权）**

Run: 同 Task 1 Step 2 的 Keil 命令。
Expected: `0 Error(s), 0 Warning(s)`，产物 `I301_code/MDK-ARM/build/I301_code/I301_code.axf/.hex` 更新。

- [ ] **Step 3: 提交**

```bash
git add I301_code/Core/Src/main.c
git commit -m "main.c: 启用AD-DA处理通路(AD_DA_Init置于param_init之后)"
```

- [ ] **Step 4: 烧录与上板验证（用户操作；对应 spec §10）**

前置：确认 **JP3 已断开**（或外部 IN± 不接）。用 Keil GUI 下载后逐项验证：

1. **波形跟随**：示波器对照 PB13 vs PA4（X 轴，重点）、PB15 vs PB12（Y 轴）——同波形、固定延迟 32–64µs、无抖动。
2. **comp 偏置**：上位机下发 0x0303 步进 ±100/±1000，求和点偏移 ≈1.22mV/码、方向正确；0x0304 落盘、断电重启后 0x0101 回读一致。
3. **并发压测**：跟随过程中持续 0x0101 查询 + 0x0302/0x0303 下发，应答无丢失、波形无毛刺。
4. **（可选）**：空闲 GPIO 翻转抽测 ISR 宽度，确认 ≤ 死线余量。

验证通过后向用户给出简要总结（AGENTS.md 要求）。
