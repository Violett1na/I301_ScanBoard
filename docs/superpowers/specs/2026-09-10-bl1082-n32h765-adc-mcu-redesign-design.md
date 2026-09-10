# I301 振镜 XY 板：外部 ADC（BL1082）+ 主控更换（N32H765） redesign 设计规格

- 日期：2026-09-10
- 状态：设计已确认（用户 2026-09-10 拍板"暂定这样，后续有问题再修改"），待转 writing-plans
- 硬件：I301X 板（现 STM32G474RBTx，LQFP64）→ 目标 N32H765（LQFP144）；ADC 由片内 12bit 改为外部 BL1082（并行/FEMC）
- 关联：`2026-08-04-ad-da-processing-path-design.md`（现 1MHz 管线，本次将其 ADC 侧替换、速率改 200kSPS）、
  `2026-08-27-ocd-pwm-signaling-design.md`（4053 X1 = OCD PWM 信令，本次保留）、
  `2026-09-02-portable-layered-architecture-design.md`（四层可移植架构，本次新增 Platform/n32h765）
- 参考：I300 工程（`D:\Desktop\I300\I300X PCB`）的 BL1082 并行拓扑（原 H753+FMC）；BL1082 手册
  `doc/BL1082/BL1082_V1.0_cn.pdf`；N32H765 手册 `CN_DS_N32H765_Series_Datasheet_V1.2.0.pdf`（均在 I300 目录）

---

## 1. 背景与目标

现板 ADC 为 STM32G474 片内 12bit：4 通道（VX/IX/VY/IY）@1MHz 逐样本，经 AD-DA 管线处理后由 4 路 DAC
回注（DA_INX/DA_FBX/DA_INY/DA_FBY）。上板测试（`I301AD波形测试.md`）显示片内 ADC 噪声/有效位不足。

用户确认的三个动机：**精度/分辨率不够**、**复用 I300 成熟方案（BL1082）**、**给 MCU 减负**。

目标：用外部 16bit 同步采样 ADC（BL1082）替换片内 ADC 采样侧；主控更换为国民技术 N32H765，
以 FEMC 并行内存映射方式读 BL1082（复刻 I300 的 H753+FMC 拓扑）；DAC 回注侧收敛为 2 路内部 DAC。

## 2. 需求与决策记录（逐项确认于 2026-09-10 商讨）

| # | 决策点 | 结论 | 依据/备注 |
|---|---|---|---|
| 1 | ADC 芯片 | BL1082（AD7606 兼容国产） | I300 已验证；16bit、8ch 同步、±5/±10V 真双极性、200kSPS |
| 2 | 速率冲突处置 | 数字通路 1MHz→**200kSPS**（路线 A） | BL1082 极限 200kSPS；振镜信号带宽 kHz 级，1MHz 为顶格冗余；延迟反降（§4） |
| 3 | 通道 | **VX/IX/VY/IY 4 路**（= 现固件 ch0~ch3）；不抽 PDA/PDB | 用户拍板；PD 数字相减方案否决（模拟位置环仍需 U3A，测量职能非痛点） |
| 4 | BL1082 接口 | **并行 16bit，经 N32H765 FEMC 内存映射** | G474 无 FMC 且 GPIO 拆端口非原子/20% CPU，否决；I300 拓扑可复用 |
| 5 | 主控 | **N32H765，LQFP144** | 四款对比（765/765EC/785/785EC）：唯一有 LQFP144；EtherCAT/双核均非需求 |
| 6 | DAC | 内部 2 路（DA_INX/DA_INY）；**DA_FBX/DA_FBY 导出删除** | N32H765 仅 2 路 DAC 可外引（另 4 路 15Msps 仅片内、无 pad、片内无 OPAMP 可中转）；导出职能已被 OCD PWM 信令取代（v2），删除无功能损失 |
| 7 | 量程 | RANGE 接 AGND = **±5V 档**（LSB=152µV）；输入箝位 ±16V 兜底 | 无 OS 时 SNR 86.2dB（ENOB≈14.1） |
| 8 | 过采样 | OS2:0 全 0（关，保 200kSPS） | OS×2 → tCONV 8.96µs → 仅 ~111kSPS；留 0R/跳线供板级试验 |
| 9 | 模拟前端 | VX/VY：U1A 去 +2V5 偏置、R70 4.99K→18K（±2.5V→±4.5V）；IX/IY：删 R64/R65/D6、末级改 0V 中心 ±4.5V | 吃满 ±5V 档；U1A 整级删除为可选项，前提=确认外部 IN± 最坏幅度 ≤±4.5V（**待用户确认，开放项 O1**） |
| 10 | 模拟伺服环 | **一字不动**（红线）：U1B/U1C/U1D/U15A/filter/U3D/U3C/U3A/VR6/AGC(MUTE)/4053 fail-safe | 位置环零延迟闭在模拟域；BL1082 仅高阻并联测量 |
| 11 | AVCC | +15V→78M05→π 型（10µ+磁珠+100n）独立模拟 5V | 现 +5V 为 SGM61430 开关轨，噪声不适合 16bit |
| 12 | VDRIVE / 基准 | VDRIVE=+3V3；BL1082 用内部 2.5V 基准；MCU VREF+=+2V5（SGM4040 保留） | DAC 1LSB≈0.61mV 语义与现固件 comp 协议不变 |

## 3. BL1082 关键事实（手册核实，设计依据）

- 16bit、8ch 同步采样 SAR，单 5V 供电；每通道 200kSPS；tCONV=4.38µs（无 OS）；tCYCLE min=5µs
- 内置二阶抗混叠滤波器，3dB@23.4kHz（200kSPS 时）→ 前端 RC 可轻（串 100R+100p 仅做驱动稳定/ESD 余量）
- 输入箝位耐受 ±16V；±5V/±10V 档由 RANGE 脚选择
- **并行模式 = PAR/SER/BYTE SEL=0**（与 AD7606 语义相反，⚠️ 不可抄 I300 strap）；并行每次 RD 脉冲输出
  一个通道（V1..V8 顺序），FRSTDATA 指示帧首
- 串行模式（本设计不用）=1，且 DOUTA/DOUTB 复用 DB7/DB8、其余 DB 接 AGND——仅作记录防误抄
- SNR：无 OS 86.6dB(±10V)/86.2dB(±5V)；OS×16 94.7dB（但吞吐降至 ~6.5kSPS，不用）

## 4. 总体架构与时序

```
 4路双极性调理 ─► BL1082 V1-V4 ─(DB15..0 并行)─► FEMC Bank(内存映射, 16bit)
   V1=VX V2=IX        │ BUSY ──► MCU EXTI            │ ISR 内 4×16bit 连续读(<1µs)
   V3=VY V4=IY        │ CONVSTA/B ◄─ TIM CH @200k    ▼
   (V5-V8 接 AGND)    │ RESET ◄─ GPIO           rx_buf[4][N] 乒乓, N=8 转换
                      │ FRSTDATA ─► GPIO(帧校验)     │ 块 ISR: 算法槽链 linear→ocd→ocd_sig
                      │                              ▼ 16bit 域运算, >>4
                      └───────────  DAC1/2(2ch 12bit, 带 Buffer) ◄─ DMA @200k (TIM TRGO)
                                      │ DA_INX/DA_INY → U1C 命令求和点（模拟伺服环入口）
                                      └ TIM PWM ×2 (1kHz/50%) → 4053 X1/Y1 = OCD 过流信令
                                          4053 选择脚 2×GPIO 推挽+下拉：默认低 = X0/Y0 模拟 FB 直通(fail-safe)
```

- **时基**：单一 TIM @200kHz：CH 输出→CONVST；TRGO→DAC 触发。收发同拍，长期不漂移
- **读取**：BUSY 下降沿 EXTI → ISR 内对 FEMC Bank 基址做 4 次连续 16bit 读（通道序 V1..V4，与固件
  ch0~ch3 一致）；FEMC 等待态按 BL1082 并行 tACC 保守配置（**开放项 O2**：实测裕量后收紧）
- **延迟预算**：半块~一块 20~40µs + tCONV 4.4µs + 读 <1µs ≈ **30~50µs**，优于现 1MHz 管线的 ≈65µs
- **CPU 预算**：200kSPS×4ch 块处理 <2%@600MHz（现 1MHz 管线为 10~20%@170MHz）
- **DAC 回注**：200kHz 零阶保持；U1C 求和网络模拟带宽 kHz 级，阶梯无影响
- **16bit 语义**：管线内部 16bit 整数运算；IN 通道 `y=(65535−x)+off16`、IX/IY 通道 `y=x+off16`
  （off16 = 协议 comp 值<<4，comp 语义仍为"DAC 码值"，协议零改动）；输出 >>4 送 12bit DAC
- **删除项**：DA_FBX/DA_FBY DAC 重建导出（含其 4 路 TX DMA 缩为 2 路）、片内 ADC 管线驱动、
  4053 选择脚的 DAC 相关旧注释语义（选择脚职能不变：OCD 信令切路）
- **保留项**：算法槽链（linear→ocd→ocd_sig）、协议层 Proto/（硬件无关）、param/flash 语义、
  AD5290 数字电位器控制、OCD 信令状态机、JP3 使用约束（§3.2 旧规格继续有效）

## 5. 硬件设计

### 5.1 BL1082 子电路（新增原理图页）

| 项 | 设计 |
|---|---|
| 封装 | LQFP-64（10×10 P0.5），复用 I300 `LQFP-64_L10.0-W10.0-P0.50-LS12.0-BL.pcbdoc` |
| 数据/控制 | DB15..DB0→FEMC D15..D0；RD/SCLK→FEMC NOE；CS→FEMC NE1；BUSY→MCU EXTI GPIO；CONVSTA=CONVSTB→TIM CH；RESET→MCU GPIO（上电复位一次）；FRSTDATA→MCU GPIO |
| strap | PAR/SER/BYTE SEL=0（接 AGND，**并行**）；RANGE=AGND（±5V）；OS2/OS1/OS0=AGND |
| 基准/去耦 | 内部 2.5V；REFCAPA/B 各 100n+22µ；REGCAPA/B 各 100n；AVCC 每电源脚 100n + 板级 10µ；VDRIVE 100n+10µ |
| 输入网络 | 每通道串 100R + 对 AGND 100p；V5~V8 输入接 AGND |
| 电源 | AVCC=+15V→78M05→π 型滤波（独立模拟 5V，与数字 +5V 单点接）；VDRIVE=+3V3 |

### 5.2 前端调理改造（每轴，driver 页原位改）

| 信号 | 现状 | 改造 |
|---|---|---|
| VX/VY（IN 采样节点） | U1A：增益 −0.499、+2V5 偏置 → 0~2.5V | 去偏置（R74/R75 分压改接地）、R70 4.99K→**18K**（增益 −1.8，±2.5V→±4.5V）。可选整级删除（O1） |
| IX/IY（电流） | U2C/U2B/U2D 三级 + R64/R65 分压 0~3.3V + D6 钳位 | U2C/U2B/U2D 保留（含电流环职能）；删 R64/R65/D6；末级偏置改 0V 中心、增益按振镜额定满量程电流映射 ±4.5V（额定值以振镜规格书为准，产线校核定标） |
| PDA/PDB→FB 链、AGC、MUTE、U1B/U1C/U1D/U15A、filter | — | **不动**（红线） |

### 5.3 MCU 区（N32H765，LQFP144）

- VDD=3.3V，片内 SMPS 外围按手册典型（VDDSMPS/VLXSMPS 电感电容）；VDDA/VREF+=+2V5（SGM4040 保留）
- USBHS 作 FS device（VDD33USB 独立 3.3）；25MHz 晶振沿用（HSE 4~32MHz 满足）；SWD 调试口
- FEMC 引脚：D0~D15 + NOE + NE1 共 18 脚；数据线串 22~33R 源端电阻、组内等长
- OCD 信令：2 路 TIM PWM 脚 → 4053 X1/Y1 新网络（替代原 PA5/PA8 网）；4053 选择脚 2 GPIO 推挽+下拉
- 释放/作废网络：原 G474 的 ADC 4 脚、DAC4 路中 DA_FB 2 路、CH-FBX/FBY 语义保留（选择脚）但改到新 MCU

### 5.4 PCB 要点

- LQFP144 与 SMPS 电感布局按手册；FEMC 16bit 并行线短走、包地、远离 LM3886 功率环路与 SGM61430 电感
- BL1082 贴近两轴前端调理之间；AVCC 磁珠后单独铺铜；AGND 星点沿用现板策略
- 8 路输入中使用的 4 路远离 MUTE/AGC 开关跳变网络

### 5.5 BOM 增量（估）

BL1082×1、N32H765(LQFP144)×1、78M05×1、SMPS 外围 2~3 件、阻容约 35 件；每轴改阻 4~6 件、删 3 件；
删 G474 及外围（晶振/复位/BM 阻容部分复用）。

## 6. 固件设计

- **分层**：新增 `Platform/n32h765/`（Port 契约 `port_*.h` 不动）；新板级头（Board 轴）描述
  4 通道语义与引脚；`BOARD_AD_DA_PIPELINE_ENABLE=1` 保持
- **驱动**：`femc_bl1082.c/.h`：FEMC 初始化（16bit、异步 PSRAM 模式、等待态按 O2 实测）；
  读 API = 4 次 volatile 16bit 连续读；BUSY EXTI 回调填 rx_buf 乒乓半块
- **管线**：TIM @200k（CH→CONVST、TRGO→DAC）；块 ISR（HT/TC）走算法槽链；tx_buf 2 通道 DMA→DAC1/2；
  延迟/相位合约沿用 2026-08-04 规格 §5（半块窗口写、退化=输出保持）
- **OCD 信令**：ocd_sig 状态机不变；PWM 改 N32H765 TIM（1kHz/50%，按 600MHz 时钟树重算 PSC/ARR）；
  选择脚 GPIO fail-safe 默认低
- **协议/参数**：Proto/ 零改动；comp/pot/param/flash 语义不变；0x0304 落盘停摆约束**沿用**
  （H765 flash 擦写同样停摆；热路径 RAM 执行列为后续项，不在本次范围）
- **删除**：片内 ADC 管线驱动（Platform/stm32g4 内 pipe 实现随板型保留不删，仅新板型不编译）、
  DA_FB 导出路径、4 路 TX DMA 中 2 路
- **时钟树**：HSE 25MHz→PLL→600MHz；外设时钟按 FEMC 100MHz 上限配置
- **引脚表**：具体 MCU 引脚分配（FEMC 18 脚、EXTI/PWM/GPIO 等）在实施计划阶段按 N32H765 手册
  pin-mux 表与原理图对账定稿；本规格只约束数量与职能（§4/§5.3）

## 7. 验证

1. 点亮序：电源轨/SMPS → 时钟+USB 枚举 → FEMC 静态回读（输入加已知直流，对照万用表/基准）
2. 管线环回：示波器 DA_INX vs VX 调理输出——同波形、延迟 30~50µs、无抖动；comp 步进方向/刻度（1.22mV/码 @求和点）回归
3. 噪声/ENOB：输入短接测底噪（目标 ≤ 现片内通路 1/4 RMS）；正弦扫频查 AAF 生效
4. 协议回归：0x0101/0x0102/0x0103/0x0301~0x0304 全命令字；并发压测（跟随中查询+下发）
5. OCD 信令：peg 注入→4053 切 X1、FB 出口 1kHz/50% 方波；释放回 X0；MCU 掉电/复位时选择脚保持低（fail-safe）
6. FEMC 裕量：收紧等待态至临界，回退一档定稿（O2）
7. 知情确认：通路运行中单次 0x0304 的"重放→跳变"属预期（约束沿用）

## 8. 风险与开放项

| # | 项 | 处置 |
|---|---|---|
| R1 | FEMC 等待态 vs BL1082 并行 tACC 裕量 | 保守起配 + §7.6 实测收紧 |
| R2 | SMPS 噪声串入 VDDA | VDDA 改独立干净 LDO 供电兜底（布局后实测决定） |
| R3 | M7 移植回归面（USB/flash/时钟树/DMA） | 分层架构隔离；§7 回归清单全跑；G474 板型/分支保留可回退 |
| R4 | BL1082/N32H765 供货 | BL1082 立创编码已核；**N32H765 立创/货期待核（O3）** |
| O1 | 外部 IN± 最坏幅度未确认 → U1A 可否整级删除 | 默认保留改值；确认后若 ≤±4.5V 可删级简化 |
| O2 | FEMC 等待态定值 | 板级实测 |
| O3 | N32H765 订货编码/货期 | 采购核实后回填 BOM |
| O4 | OS×2（~111kSPS，+3dB SNR）是否有必要 | 留 0R 选项，板级 A/B |

## 附：被否决的替代方案

| 方案 | 否决原因 |
|---|---|
| G474 + BL1082 串行（SPI 双线/单线 DMA） | MCU 更换后不再需要；且串行驱动复杂度高于 FEMC 内存映射 |
| G474 + BL1082 并行（GPIO 拆端口/位敲） | 无 FMC；16bit 无单端口可原子读；~20% CPU；20+ 引脚 |
| 维持 G474 + 片内 ADC | 不解决精度痛点（用户动机 1） |
| N32G4FR（XFMC、LQFP64/80） | 仅 2 路外引 DAC 同缺口，且 144MHz/小封装无收益；被 N32H765 覆盖 |
| N32H785/785EC（双核/EtherCAT） | 双核仅解决 flash 停摆等有更便宜单核解法的问题；EtherCAT 非需求；封装只到 LQFP176/BGA |
| PDA/PDB 直入 ADC + 数字相减 | 模拟位置环仍需 U3A；测量职能非痛点；通道数与前端改动翻倍 |
| 保 1MHz 换更快 ADC（AD7616/ADS8588 级） | 偏离复用 I300 初衷；选型/供货/接口全重研 |
| 内部 4 路 15Msps DAC 经 OPAMP 外引 | N32H765 无 OPAMP 模块；该 4 路 DAC 无焊盘，信号不出 die |
