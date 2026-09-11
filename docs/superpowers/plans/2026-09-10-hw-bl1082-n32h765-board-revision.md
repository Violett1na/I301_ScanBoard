# I301X 硬件改版（BL1082 + N32H765）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> **本计划特殊说明**：交付物为 Altium 工程文件与制板输出，执行主体为硬件工程师（用户）在 Altium 中手工操作；
> 每个 Task 的"验证"即该 Task 的测试环节（ERC/网表差分/目视审查），不通过不进入下一 Task。

**Goal:** 将 I301X 板 ADC 采样侧改为 BL1082（FEMC 并行）、主控改为 N32H765(LQFP144)、DAC 收敛 2 路，产出可投板的改版工程。

**Architecture:** 层次化原理图原位改版（driver/power 页改值、mcu 页重画、新增 adc 页）；PCB 新建 I301X5.PcbDoc；
模拟伺服环全部网络零改动（红线），改动集中在 ADC 采样链、MCU 区、电源 AVCC 支路。

**Tech Stack:** Altium Designer（SchDoc/PcbDoc/OutJob）、BL1082（LQFP64）、N32H765（LQFP144）、78M05。

## Global Constraints

- 规格源：`docs/superpowers/specs/2026-09-10-bl1082-n32h765-adc-mcu-redesign-design.md`（下称 SPEC）；冲突以 SPEC 为准并回改本计划
- 红线网络（零改动）：U1B/U1C/U1D/U15A/filter 子图/U3D/U3C/U3A/VR6/AGC(U3B,Q4)/MUTE(Q1,Q2)/4053 的 X0/Y0 与公共端出口（SPEC §2 决策 10）
- BL1082 strap：**PAR/SER/BYTE SEL=AGND（并行）**；RANGE=AGND（±5V）；OS2/OS1/OS0=AGND（SPEC §3、§5.1）
- 基准语义：MCU VREF+=+2V5（SGM4040 保留）；BL1082 用内部 2.5V（SPEC §2 决策 12）
- 提交纪律：PCB 仓库（`D:\Desktop\I301\I301X`）每个 Task 结束提交一次，提交信息须经用户批准（全局红线）
- 命名/注释跟随现工程惯例（中文注释、网络名大写字母数字）

---

### Task 1: 基线归档与工程准备

**Files:**
- 读取基线：`D:\Desktop\I301\I301X\PCB\I301X4.net`、`I301X.pdf`（8 页，2026-08-21 版）
- 创建：`D:\Desktop\I301\I301X\PCB\baseline-2026-09-10\`（归档目录）

- [ ] **Step 1: 归档基线**：复制 `I301X4.net`、`I301X.pdf`、`I301X4.PcbDoc` 到 `baseline-2026-09-10\`
- [ ] **Step 2: 导出当前原理图网表**（Altium: Design→Netlist→Protel）存为 `baseline-2026-09-10\I301X-pre.net`
- [ ] **Step 3: 验证**：`baseline-2026-09-10\` 含 4 个文件；`I301X-pre.net` 与 `I301X4.net` 网络数一致（容差：仅排序差异）
- [ ] **Step 4: 提交**（信息待用户批准，下同）：`git add baseline-2026-09-10 && git commit`

### Task 2: 新增 adc.SchDoc（BL1082 子电路）

**Files:**
- 创建：`D:\Desktop\I301\I301X\PCB\adc.SchDoc`；修改：`I301X.PrjPcb`（挂页）、`I301X.SchDoc`（加 sheet symbol）
- 参考封装：`D:\Desktop\I300\I300X PCB\PCB\SCANXY-2681\SCANXY-268_1.SCHLIB`（BL1082 符号）与
  `D:\Desktop\I300\I300X PCB\doc\AD7606\LQFP-64_L10.0-W10.0-P0.50-LS12.0-BL.pcbdoc`（封装）

**Interfaces:**
- Produces（层次端口，供 mcu 页连接）：`FEMC_D0..FEMC_D15`、`FEMC_NOE`、`FEMC_NE1`、`BL_BUSY`、`BL_CONVST`、`BL_RESET`、`BL_FRSTDATA`
- Consumes：电源 `+5VA`(AVCC)、`+3V3`(VDRIVE)、`AGND`

- [ ] **Step 1: 建符号/器件**：从 I300 SCHLIB 导入 BL1082 符号与 LQFP64 封装，核对引脚表与 `BL1082_V1.0_cn.pdf` §3.1 一致（64 脚全对账）
- [ ] **Step 2: 画子电路**（值照 SPEC §5.1）：DB15..DB0→端口 FEMC_D15..0；RD/SCLK→FEMC_NOE；CS→FEMC_NE1；BUSY→BL_BUSY；CONVSTA+CONVSTB 短接→BL_CONVST；RESET→BL_RESET；FRSTDATA→BL_FRSTDATA；PAR/SER/BYTE SEL、RANGE、OS0/1/2 各经 0R 接 AGND；REFCAPA/B 各 100n+22µ 到 AGND；REGCAPA/B 各 100n；AVCC 脚各 100n + 10µ；VDRIVE 100n+10µ
- [ ] **Step 3: 输入网络**：V1..V4 各串 100R（位号 R_BL1..R_BL4）+ 对 AGND 100p（C_BL1..C_BL4），输入端接层次端口 `ADC_IN1..ADC_IN4`；V5..V8 经 0R 接 AGND
- [ ] **Step 4: 验证**：ERC 无 error；对照 SPEC §5.1 表逐行目视核对 strap 与去耦；导出 adc 页 PDF 自审
- [ ] **Step 5: 提交**

### Task 3: mcu.SchDoc 重画（N32H765 LQFP144）

**Files:**
- 修改：`D:\Desktop\I301\I301X\PCB\mcu.SchDoc`（整页重画）；新建符号库件：N32H765 LQFP144
- 数据手册：`D:\Desktop\I300\I300X PCB\CN_DS_N32H765_Series_Datasheet_V1.2.0.pdf`（引脚表/pin-mux）

**Interfaces:**
- Consumes：Task 2 的 FEMC/BL_* 端口；driver 页的 `ADC_IN1..4`（经调理）、`DA_INX/DA_INY`、`PWM_OCD_X/Y`、4053 选择脚 `CH_FBX/CH_FBY`
- Produces：USB、SWD、UART、AD5290 六组 SCL/SDA+CS、LED、TX/RX（职能与现页一致，脚位重排）

- [ ] **Step 1: 引脚分配表**：按手册 pin-mux 定 FEMC D0-15/NOE/NE1 脚位（选 16bit 非复用组）、
  **DAC 仅可用手册标明"支持对外输出带/不带 Buffer"的 2 路（1Msps 那两路；另 4 路 15Msps 仅片内、严禁选用）**、
  TIM PWM×2、EXTI(BL_BUSY)、GPIO(BL_RESET/BL_FRSTDATA)、TIM CONVST CH 脚、USBHS FS 脚、SWD、UART、AD5290 GPIO×13、LED×3；
  **输出引脚表文档** `mcu-pinmap-2026-09-10.md`（放 PCB 目录，供固件计划消费）
- [ ] **Step 2: 画 MCU 页**：N32H765 符号；电源：VDD=+3V3、VDDSMPS/VLXSMPS 按手册典型 SMPS 外围（电感+电容值抄手册典型电路）、VDDA/VREF+=+2V5（C 1µ）、VDD33USB=+3V3、VBAT 经 1µ 到 +3V3；去耦按手册；25MHz 晶振+2×22p（沿用 X1 网络 OSC_I/OSC_O）；复位 10K+100n+1N4148（沿用现拓扑）；BOOT0 下拉 10K
- [ ] **Step 3: 连接网络**：FEMC/BL_* 接 adc 页端口；DAC 两路→`DA_INX/DA_INY`；PWM×2→`PWM_OCD_X/Y`；选择脚→`CH_FBX/CH_FBY`（推挽+下拉在 MCU 页画 10K 下拉）；删除旧网络：`ADC-VX/VY/IX/IY` 到 MCU 的连线、`DAC3/DAC4`(DA_FB) 网络、旧 ADC 引脚占用
- [ ] **Step 4: 保留页内容**：SGM4040(+2V5)、SWD 座 J2、LED、USB-C 不在本页（顶层）不动
- [ ] **Step 5: 验证**：ERC 无 error；引脚表与原理图逐脚对账（双人/双人份目视：自己隔一天复审一次）；未用脚按手册处置（建议配上下拉或悬空按手册表）
- [ ] **Step 6: 提交**

### Task 4: driver.SchDoc 前端改造（每轴实例）

**Files:**
- 修改：`D:\Desktop\I301\I301X\PCB\driver.SchDoc`（X/Y 两实例对称改）

- [ ] **Step 1: VX/VY 级**：U1A 的 +2V5 偏置分压 R74/R75 改为对 AGND（去偏置）；反馈 R70 4.99K→**18K**（增益 −1.8）；输出网络 `ADC-V` 改名/改接为层次端口 `ADC_IN1`(X)/`ADC_IN3`(Y) 至 adc 页
- [ ] **Step 2: IX/IY 级**：删除 R64、R65、D6（0~3.3V 分压+钳位）；U2D 输出改接 `ADC_IN2`(X)/`ADC_IN4`(Y)；增益改值计算：采样电阻 0R1，目标总增益 G=4.5V/(0.1Ω×I_rated)，I_rated 取振镜规格书额定满量程电流；
  **增益修改全部落在末级 U2D**（反馈/输入电阻比 = G/前级实测增益），前级 U2C/U2B 不动；偏置改 0V 中心
  （去除原单极性偏置网络，同相端直地或对称分压）；改值后验证：输入端串精密电阻注入已知直流
  （等效 ±I_rated/2），U2D 输出应为 ±2.25V±2%
- [ ] **Step 3: 4053 X1/Y1 输入**：原 PA5/PA8(DA_FB) 网络改接 `PWM_OCD_X/Y`（来自 mcu 页）；X0/Y0、公共端、R6/R8 出口**不动**；选择脚网络 CH_FBX/CH_FBY 不动
- [ ] **Step 4: 红线核对**：U1B/U1C/U1D/U15A/VR2/JP3/R62/R16/R24、filter 子图、U3 链、AGC、MUTE、去耦——逐件确认零改动（对照 baseline 网表该子图段 diff 为空）
- [ ] **Step 5: 验证**：ERC 无 error；driver 页导出 PDF 与 baseline 页并排目视：改动仅限 Step 1-3 清单
- [ ] **Step 6: 提交**

### Task 5: power.SchDoc 增 AVCC 支路

**Files:**
- 修改：`D:\Desktop\I301\I301X\PCB\power.SchDoc`

- [ ] **Step 1: 加 78M05**：输入 +15V（C 10µ+100n）→78M05→输出 C 10µ+100n→磁珠(600R@100MHz)→`+5VA` 网络（adc 页 AVCC）；+5VA 对 AGND 再 10µ+100n；与数字 +5V 单点接于 78M05 输入侧
- [ ] **Step 2: 验证**：ERC 无 error；功耗核算：BL1082 工作电流（手册典型值）×(15−5)V < 78M05 封装耗散限额（TO-220 无散热片 ≤1W 判据），超限则改输入源为 +5V 前级或加散热
- [ ] **Step 3: 提交**

### Task 6: 全图 ERC 与网表差分

**Files:**
- 产出：`D:\Desktop\I301\I301X\PCB\baseline-2026-09-10\I301X-post.net`、差分记录 `net-diff-2026-09-10.md`

- [ ] **Step 1: 导出新网表** 存 I301X-post.net
- [ ] **Step 2: 差分**（对 I301X-pre.net）：期望**新增**网络=FEMC_D0..15、FEMC_NOE、FEMC_NE1、BL_BUSY、BL_CONVST、BL_RESET、BL_FRSTDATA、ADC_IN1..4、+5VA、PWM_OCD_X/Y；期望**删除**=旧 ADC-VX/VY/IX/IY→MCU 段、DA_FBX/DA_FBY、DAC3/DAC4 网络；期望**零变化**=红线网络全集（U1B/U1C/U1D/U15A/U3 链/AGC/MUTE/4053 X0 出口/±15V/±24V/+2V5/+3V3）
- [ ] **Step 3: 差分结果写入 net-diff 文档**，任何期望外差异逐条归因后才可进入 Task 7
- [ ] **Step 4: 全工程 ERC + 原理图 PDF 导出（替换 I301X.pdf）**
- [ ] **Step 5: 提交**

### Task 7: PCB 新板 I301X5.PcbDoc

**Files:**
- 创建：`D:\Desktop\I301\I301X\PCB\I301X5.PcbDoc`（自 I301X4 另存后改）；修改：`I301X.PrjPcb` 指向

- [ ] **Step 1: 器件更新**：MCU 换 LQFP144 封装、新增 BL1082(LQFP64)、78M05、磁珠、阻容增量；删除 G474 封装
- [ ] **Step 2: 布局**：N32H765 原位扩展区；BL1082 置于两轴前端调理之间；78M05+磁珠靠近 BL1082；SMPS 电感按手册远离 VDDA 走线
- [ ] **Step 3: 布线**：FEMC 16 数据+NOE+NE1 短走、组内等长±5mil、串 22~33R 源端、包地；+5VA 磁珠后单独铺铜接 BL1082 AVCC；AGND 星点沿用；红线模拟区走线不动
- [ ] **Step 4: DRC + 网表比对（原理图 vs PCB 网表一致）**
- [ ] **Step 5: 评审输出**：装配图/PDF 3D 视图目视；提交

### Task 8: 制板输出与 BOM

**Files:**
- 产出：Gerber/钻孔（OutJob）、`I301X5_bom.xlsx`

- [ ] **Step 1: BOM 编制**：增量器件带立创编码；**N32H765 编码/货期核实回填（SPEC 开放项 O3）**；BL1082 编码 C?（I300 记录核对）
- [ ] **Step 2: OutJob 出 Gerber/钻孔/钢网/坐标**；叠层沿用 I301X4
- [ ] **Step 3: 投板前检查单**：SPEC §7.1 点亮序对应的测试点是否都有测点/过孔露铜（+5VA、VDD、VDDA、VREF+、DA_INX/Y、FEMC D0/D15、BL_BUSY）
- [ ] **Step 4: 提交**（投板动作本身须用户另行授权，全局红线）

---

## 后续计划（不在本文件）

- 计划二 `2026-09-XX-fw-n32h765-bl1082-pipeline.md`：固件移植（Platform/n32h765、femc_bl1082 驱动、管线 200k、OCD PWM 重配、删除项、回归），前置=国民 N32H765 固件库入仓 + Task 3 引脚表
- 计划三（可并入计划二尾部）：新板 bring-up 与 SPEC §7 验证序列
