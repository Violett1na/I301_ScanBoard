# I301 振镜 XY 板固件 Release 说明（2026-08-28）

基线：master（= Debug 分支合入 + release 清理）。开发全过程记录在 Debug 分支历史与
`docs/superpowers/specs/`、`docs/superpowers/plans/` 下。

## 主要功能

1. **AD-DA 1MHz 实时跟随通路**
   - TIM3 TRGO(1MHz) → 4 路 ADC（vx/ix/vy/iy）循环 DMA → 半块乒乓处理 → 4 路 DAC 循环 DMA。
   - IN 通道（vx→DA_INX、vy→DA_INY）：反相线性 `y = (4095 + comp) - x`，抵消调理反相；
     FB 通道（ix→DA_FBX、iy→DA_FBY）：同相 `y = x + comp`，静息 0V。
   - 块长 `AD_DA_BLOCK=32`：每拍 16µs/2720 周期预算，全链占用约 50%。
     ⚠️ 勿改小：16（8µs 预算）时全链贴穿预算，上电约 255ms 整机冻结（2026-08-28 根因，
     详见 Debug 分支提交 b65daf6 说明）。
2. **过流 PWM 信令（ocd_sig）**
   - 按轴独立检测：ix/iy 连续 >8 个样本撞量程轨（==0 或 ==4095）即跳闸，
     切 4053 选择脚（PB3/PB4），把该轴 FB 出口从模拟直通切到常开 1kHz/50%/3V3 方波
     （TIM2_CH1→PA5 / TIM1_CH1→PA8），报总控板切激光。
   - 防抖释放：无 peg 稳定满 1s 回模拟直通。
   - 只发信令、不动图像：原 `ocd.c` 命令级干预（跳闸强置中点）经 `OCD_ENABLE=0`
     编译期摘除（保留宏骨架，置 1 可恢复对照）。
   - DAC 反馈功能已废弃：DAC1_CH2/DAC4_CH2/OPAMP5 运行时停止让路，
     PA5/PA8 定死 PWM（再配置清单见 `ocd_sig.c` 文件头）。
3. **参数持久化与上位机协议**
   - flash 存储 radc（AD5290 数字电位器码值）/comp（偏置补偿），上电加载；
   - USB bulk（lsnet）协议：读写电位器、设置补偿、保存参数、设备信息、复位。
4. **启动时序**
   - 外设初始化 → flash 参数 → AD5290 → USB → 协议栈 → 启动管线 →
     过流检测挂载 → LED_USB 闪 6 次（150ms 间隔）→ 主循环（纯跟随 + USB 收包）。

## 关键配置

| 项 | 值 | 说明 |
|---|---|---|
| `AD_DA_BLOCK` | 32 | 冻结修复值，勿改小 |
| `OCD_SIG_ENABLE` | 1 | 信令总开关 |
| `OCD_SIG_FORCE` | — | release 已删除（诊断用） |
| `OCD_ENABLE` | 0 | 图像干预摘除；置 1 恢复 200ms 强置+1s 盲期 |
| 优化等级 | ≥ level-2 | level-0 构建必冻结（历史结论，§9.12） |

## 已知边界

- **JP3 必须断开**（两源叠加禁令，原仓库 spec §3.2）。
- **上电瞬间假跳闸**：`comp=0000` 且 ix 未建立时读 0，触发约 1s 的信令高电平
  （PB3/PB4 上电高约 1s 后释放）。电气上不可接受时后续加启动门。
- 真过流联调中：检测判据（撞轨判据）对"未冲轨的边际过流"不敏感，联调反馈后再议阈值化。
- 架构待加固（可选）：管线中断优先级 0 + SysTick 最低 15 的组合在预算被穿时
  会整机冻结且自锁；可改管线降级 + SysTick 提权。

## 诊断功能去向

MSB 对照测试、点阵直出、常量断环、死区/低滤/中值、ADC_SOLO、OCD_SIG_FORCE、
宿主单测等全部诊断资产**保留在 Debug 分支**（master release 已移除），
需要时从 Debug 分支取用或对照。
