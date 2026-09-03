/* port_pipe_g4.c —— STM32G4 实现: AD-DA 1MHz 实时管线
 * 2026-09-02 分层重构: 原 App/ad_da.c 的管线机制(校准/重配/预填/
 *   手工 TX DMA/NVIC/时钟释放时序)整体下沉至此; 线性算法上移
 *   App/ad_da_alg.c, 算法槽经 set/get 接口由主侧装配。
 * 上下游: main.c 经 port_pipe_init 启动; ocd/ocd_sig 经算法槽包装;
 *   stm32g4xx_it.c extern 本文件的 4 路 TX DMA 句柄(传输错误兜底)。
 * 再配置清单(对生成配置的运行时覆盖, 集中在 port_pipe_init 内):
 *   ① DAC 触发 —— MX_DAC1_Init/MX_DAC4_Init 原配触发被运行时重配为
 *      TIM3 TRGO, 缓冲保持 OFF(不动 .ioc/生成代码);
 *   ② DAC4 输出连接 —— CH1/CH2 改片内连接(进 OPAMP4/5),
 *      DAC1 两通道保持直接出脚;
 *   ③ RX ADC DMA —— hadc2/3/4/5 的循环 DMA 改指本模块 rx_buf 并启动;
 *   ④ NVIC —— DMA1_Ch5~Ch8(TX 传输错误, 优先级 1)与
 *      TIM6_DAC/TIM7_DAC(欠载兜底, 优先级 2)在此使能;
 *      DMA1_Ch1(RX 节拍, 优先级 0)由生成代码设置、此处仅放行;
 *   ⑤ TX DMA —— .ioc 无 DAC DMA, 本模块手工创建 4 路并链接到
 *      hdac1/hdac4 句柄(__HAL_LINKDMA)。
 * 回退边界: BOARD_AD_DA_PIPELINE_ENABLE=0 的板型不编译本文件。
 * 中断预算: 块处理节拍优先级 0、死线 = AD_DA_HALF µs; 详见下方
 *   AD_DA_BLOCK 警示注释(先例 b65daf6)。 */

#include "port_pipe.h"
#include "adc.h"
#include "dac.h"
#include "tim.h"
#include "opamp.h"
#include "main.h"

/* ------------------------------------------------------------------
 * ⚠️ 块长即中断预算: 每拍预算 = AD_DA_HALF × 1µs × 170MHz。
 * 16(8µs/1360周期)时全链(ocd_sig+ocd+线性, 实测≈1200-1400周期)贴线偏超,
 * 上电靠 ocd 跳闸 HOLD 的廉价路径侥幸存活, HOLD 结束(200ms)即整机冻结
 * (2026-08-28 根因, 详见记忆); 32(16µs/2720周期)占用率≈50%, 裕量充足。
 * 勿再改小; 如需更低延迟先重测全链周期开销。
 * 注: 块长是平台预算参数(本实现按 G4 @170MHz 实测), 换芯片须重测重定。
 * ------------------------------------------------------------------ */
#define AD_DA_BLOCK   32U                  /* RX/TX 乒乓缓冲每通道样本数 */
#define AD_DA_HALF    (AD_DA_BLOCK / 2U)   /* 半块长度 = 块处理单位 */

/* ------------------------------------------------------------------
 * 数据流: TIM3 TRGO(1MHz) → 4路ADC → rx_buf → 块处理 → tx_buf → 4路DAC
 *   rx_buf[0]=vx(ADC3/PB13) → tx_buf[0] → DAC1_CH1 → PA4  (DA_INX)
 *   rx_buf[1]=ix(ADC2/PA0)  → tx_buf[1] → DAC1_CH2 → PA5  (DA_FBX)
 *   rx_buf[2]=vy(ADC4/PB15) → tx_buf[2] → DAC4_CH1 → PB12 (DA_INY, 经OPAMP4跟随)
 *   rx_buf[3]=iy(ADC5/PA9)  → tx_buf[3] → DAC4_CH2 → PA8  (DA_FBY, 经OPAMP5跟随)
 *
 * 半块相位合约(无锁核心): RX 半块完成时刻 = TX 进入同一半块时刻(同 TRGO)
 * ⇒ ISR 拥有完整 AD_DA_HALF µs 窗口(现配置 = 16µs)写该半块；
 * 若超死线, TX 重读旧值 = 输出保持, 不崩。
 * 注意: 块处理必须 << AD_DA_HALF µs, 否则同级中断背靠背、CPU 被锁死在 ISR。
 * (上板教训: -O0 朴素双层循环 >预算, TIM3 一启动整机即冻结; -O2 下
 *  AD_DA_BLOCK=16 时全链贴穿 8µs 预算, ~255ms 冻结自锁, 见 b65daf6;
 *  现 AD_DA_BLOCK=32, 预算 16µs/2720 周期, 占用约 50%。勿再改小。)
 * ------------------------------------------------------------------ */
static uint16_t rx_buf[AD_DA_CH_NUM][AD_DA_BLOCK];   /* RX: ADC 循环 DMA 写入 */
static uint16_t tx_buf[AD_DA_CH_NUM][AD_DA_BLOCK];   /* TX: DMA 循环读出至 DAC DHR */

/* 算法槽: 平台私有持有, 须在 port_pipe_init 前经 ad_da_process_fn_set
 * 装配(默认算法在 App 层)。
 * 并发关系: 主循环写(启动期 set/ocd 与 ocd_sig 包装, 此时管线已启动、
 * ISR 在跑), ISR 每拍读 —— ISR/主循环共享, 按规范 5-2 加 volatile;
 * 单对齐字写入原子, 不加锁(承自原设计)。 */
static volatile ad_da_process_fn_t s_process_fn = (ad_da_process_fn_t)0;

/* 监测快照: ISR 每块写一次, 外部经 port_pipe_snapshot() 只读 */
static volatile adc_value_t s_adc_value;

/* TX DMA 句柄(.ioc 无 DAC DMA, 手工创建), stm32g4xx_it.c 中 extern 引用 */
DMA_HandleTypeDef hdma_dac1_ch1;
DMA_HandleTypeDef hdma_dac1_ch2;
DMA_HandleTypeDef hdma_dac4_ch1;
DMA_HandleTypeDef hdma_dac4_ch2;

void ad_da_process_fn_set(ad_da_process_fn_t fn)
{
    s_process_fn = fn;
}

ad_da_process_fn_t ad_da_process_fn_get(void)
{
    return s_process_fn;
}

const volatile adc_value_t *port_pipe_snapshot(void)
{
    return &s_adc_value;
}

/* 块处理: 处理自 offset 起的一个半块(全部通道) */
static void ad_da_process_half(uint16_t offset)
{
    const uint16_t *in[AD_DA_CH_NUM];
    uint16_t       *out[AD_DA_CH_NUM];
    uint8_t         ch;

    for (ch = 0U; ch < AD_DA_CH_NUM; ch++)
    {
        in[ch]  = &rx_buf[ch][offset];
        out[ch] = &tx_buf[ch][offset];
    }
    s_process_fn(in, out, AD_DA_HALF);
}

/* 块处理节拍: 仅以 hadc3(DMA1_Ch1)的 HT/TC 为全局调度点。
 * ISR 内容为纯数据搬运 + 整数 ALU(实时数据通路, AGENTS 中断规范
 * 的已确认例外条款), 严禁在此调用协议/flash/日志。
 * 上下文: 优先级 0(dma.c 设), 死线 = AD_DA_HALF µs = 16µs;
 * 最坏消耗见 App/ad_da_alg.c 头注释(全链 ≈1200-1400 周期)。 */
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
        s_adc_value.vx = rx_buf[0][AD_DA_BLOCK - 1U];
        s_adc_value.ix = rx_buf[1][AD_DA_BLOCK - 1U];
        s_adc_value.vy = rx_buf[2][AD_DA_BLOCK - 1U];
        s_adc_value.iy = rx_buf[3][AD_DA_BLOCK - 1U];
    }
}

/* 创建 1 路 TX DMA: 内存→外设、循环, DMAMUX 请求挂 DAC 通道。
 * 外设侧宽度必须 WORD: G4 的 DAC DHR 寄存器只支持 32 位写, 半字/字节写会
 * 直接触发 DMA 总线传输错误 TEIF(上板实测: 4 路通道首拍全部报错停摆)。
 * 内存侧保持 HALFWORD, DMA 自动零扩展打包为 32 位写, DHR 仅取低 12 位。
 * (F1/F4 家族 DHR 可半字写, 网上大量例程为旧家族写法, 不可照搬) */
static void tx_dma_create(DMA_HandleTypeDef *hdma, DMA_Channel_TypeDef *ch, uint32_t request)
{
    hdma->Instance                 = ch;
    hdma->Init.Request             = request;
    hdma->Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma->Init.MemInc              = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma->Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma->Init.Mode                = DMA_CIRCULAR;
    hdma->Init.Priority            = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(hdma) != HAL_OK)
    {
        Error_Handler();
    }
}

/* AD-DA 通路初始化(时序合约: TX 先于 RX、时钟最后释放):
 * 校准 → DAC 触发重配 → 输出使能 → 初值/预填 → TX DMA 创建 →
 * RX DMA 启动 → TIM3 释放。HAL 失败返回 -1, 调用方走致命错误路径。 */
int port_pipe_init(void)
{
    DAC_ChannelConfTypeDef sConfig = {0};

    if (s_process_fn == (ad_da_process_fn_t)0)
    {
        return -1;   /* 算法槽未装配: 主侧须先 ad_da_process_fn_set */
    }

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
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK)
    {
        return -1;
    }
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;   /* DAC4: 片内进 OPAMP4/5 */
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_1) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_2) != HAL_OK)
    {
        return -1;
    }


    /* 3. 先使能输出端 */
    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_Start(&hdac4, DAC_CHANNEL_1) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_Start(&hdac4, DAC_CHANNEL_2) != HAL_OK)
    {
        return -1;
    }
    if (HAL_OPAMP_Start(&hopamp4) != HAL_OK)
    {
        return -1;
    }
    if (HAL_OPAMP_Start(&hopamp5) != HAL_OK)
    {
        return -1;
    }

    /* 4. 初值 + tx_buf 整体预填(防前半块垃圾值):
          IN 通道(CH1) 2048 中点; FB 通道(CH2) 0 —— 静息 0V, 与算法/原仓库
          FB 静息 0 意图一致(spec §9.7) */
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0) != HAL_OK)
    {
        return -1;
    }
    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        const uint16_t idle_val = ((ch == 1U) || (ch == 3U)) ? 0U : 2048U;
        for (uint16_t i = 0; i < AD_DA_BLOCK; i++)
        {
            tx_buf[ch][i] = idle_val;
        }
    }

    /* 5. 手工创建 4 路 TX DMA(DMA1_Ch5–Ch8), 链接到 DAC 句柄 */
    tx_dma_create(&hdma_dac1_ch1, DMA1_Channel5, DMA_REQUEST_DAC1_CHANNEL1);
    __HAL_LINKDMA(&hdac1, DMA_Handle1, hdma_dac1_ch1);
    tx_dma_create(&hdma_dac4_ch1, DMA1_Channel7, DMA_REQUEST_DAC4_CHANNEL1);
    __HAL_LINKDMA(&hdac4, DMA_Handle1, hdma_dac4_ch1);
    tx_dma_create(&hdma_dac1_ch2, DMA1_Channel6, DMA_REQUEST_DAC1_CHANNEL2);
    tx_dma_create(&hdma_dac4_ch2, DMA1_Channel8, DMA_REQUEST_DAC4_CHANNEL2);
    __HAL_LINKDMA(&hdac1, DMA_Handle2, hdma_dac1_ch2);
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

    /* 启动 4 路 TX DMA(循环、长度 AD_DA_BLOCK), 启动后即消费预填值 */
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)tx_buf[0], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, (uint32_t *)tx_buf[1], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_Start_DMA(&hdac4, DAC_CHANNEL_1, (uint32_t *)tx_buf[2], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK)
    {
        return -1;
    }
    if (HAL_DAC_Start_DMA(&hdac4, DAC_CHANNEL_2, (uint32_t *)tx_buf[3], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK)
    {
        return -1;
    }

    /* DAC 欠载兜底中断: HAL_DAC_Start_DMA 已使能 DMAUDRIE, 此处释放 NVIC。
       TIM6_DAC 线挂 DAC1&DAC3、TIM7_DAC 线挂 DAC2&DAC4; 优先级 2
       (低于 RX 节拍 0 与 TX 传输错误 1), handler 见 stm32g4xx_it.c:
       清欠载标志并继续运行, 严禁落入 Default_Handler 死锁 */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 2, 0);
    HAL_NVIC_SetPriority(TIM7_DAC_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    HAL_NVIC_EnableIRQ(TIM7_DAC_IRQn);

    /* 循环 DMA 周期性置位 HT/TC(TC 周期 = AD_DA_BLOCK µs = 32µs),
       本设计 TX 侧无需块中断: 屏蔽 HT/TC、仅保留 TE(传输错误),
       避免高频空中断 */
    __HAL_DMA_DISABLE_IT(&hdma_dac1_ch1, DMA_IT_HT | DMA_IT_TC);
    __HAL_DMA_DISABLE_IT(&hdma_dac1_ch2, DMA_IT_HT | DMA_IT_TC);
    __HAL_DMA_DISABLE_IT(&hdma_dac4_ch1, DMA_IT_HT | DMA_IT_TC);
    __HAL_DMA_DISABLE_IT(&hdma_dac4_ch2, DMA_IT_HT | DMA_IT_TC);

    /* 6. RX DMA 改指 rx_buf 并启动; 使能 hadc3 DMA 中断(块处理节拍, 优先级已在 dma.c 设 0) */
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    if (HAL_ADC_Start_DMA(&hadc3, (uint32_t *)rx_buf[0], AD_DA_BLOCK) != HAL_OK)
    {
        return -1;
    }
    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)rx_buf[1], AD_DA_BLOCK) != HAL_OK)
    {
        return -1;
    }
    if (HAL_ADC_Start_DMA(&hadc4, (uint32_t *)rx_buf[2], AD_DA_BLOCK) != HAL_OK)
    {
        return -1;
    }
    if (HAL_ADC_Start_DMA(&hadc5, (uint32_t *)rx_buf[3], AD_DA_BLOCK) != HAL_OK)
    {
        return -1;
    }

    /* 7. 唯一时钟源最后释放: 收发同拍锁相 */
    if (HAL_TIM_Base_Start(&htim3) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

/* file end */
