#include "task.h"
#include "bsp_flash.h"

flash_store_t flash_store;
volatile adc_value_t adc_value;

radc_value_t radc_value;
comp_value_t comp_value;

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
 * ⇒ ISR 拥有完整 32µs 窗口写该半块；若超死线, TX 重读旧值 = 输出保持, 不崩。
 * 注意: 块处理必须 << 32µs, 否则同级中断背靠背、CPU 被锁死在 ISR
 * (上板教训: -O0 下朴素双层循环 128 样本 >32µs, TIM3 一启动整机即冻结)。
 * ------------------------------------------------------------------ */
static uint16_t rx_buf[AD_DA_CH_NUM][AD_DA_BLOCK];   /* RX: ADC 循环 DMA 写入 */
static uint16_t tx_buf[AD_DA_CH_NUM][AD_DA_BLOCK];   /* TX: DMA 循环读出至 DAC DHR */

/* 默认线性算法(IN/FB 通道分制, 饱和钳位 0..4095):
 *   IN 通道 ch0/ch2(vx→DA_INX, vy→DA_INY): y = (4095 - x) + off
 *     —— 符号分析见 spec §1: 抵消 ADC-V 调理反相, 进 U1C 求和点端到端符号为正;
 *   FB 通道 ch1/ch3(ix→DA_FBX, iy→DA_FBY): y = x + off
 *     —— 反馈导出要求(2026-08-06 上板确认, spec §9.7): 静息 0V、有激励时出波形,
 *     不反相; 静息 i≈0 ⇒ y=off 钳到 0; 原统一反相公式会把 FB 静息点顶到 ≈2.2V。
 * off 取协议补偿值: ch0/ch1(X轴)用 comp_value.x, ch2/ch3(Y轴)用 comp_value.y
 * (comp_value 主循环写、ISR 读, 对齐加载天然原子, 不加锁)
 * 刻度: 1 LSB ≈ 0.61mV(DAC端) ≈ 1.22mV(模拟求和点, 经 U15 ×2)；off 为偏置叠加(调零), 非校准
 *
 * 性能要点(32µs 死线, 每次 4 通道 × 32 样本; 本工程按 -O0 构建, 每周期都很贵):
 * 1) off 预合并为 base, 每样本只剩一次加/减法;
 * 2) 指针步进代替二维下标, 消除 -O0 下双层索引的栈往返;
 * 3) __USAT(val,12) 为 M4 硬件饱和指令, 单指令完成 0..4095 双向钳位;
 * 4) 循环二重展开, 减半循环记账开销;
 * 5) 反相/同相分支在样本循环之外(每通道判定一次, 无逐样本判断)。
 * 若死线仍吃紧, 可在 EIDE 中仅对 task.c 提高优化等级(其余文件保持 -O0)。 */
void ad_da_process_linear(const uint16_t *in[AD_DA_CH_NUM],
                          uint16_t       *out[AD_DA_CH_NUM],
                          uint16_t        n)
{
    int32_t base[AD_DA_CH_NUM];
    uint8_t inv[AD_DA_CH_NUM];

    /* IN 通道: y = (4095 + off) - x */
    base[0] = 4095 + (int32_t)comp_value.x;   inv[0] = 1U;   /* vx → DA_INX */
    base[2] = 4095 + (int32_t)comp_value.y;   inv[2] = 1U;   /* vy → DA_INY */
    /* FB 通道: y = off + x */
    base[1] = (int32_t)comp_value.x;          inv[1] = 0U;   /* ix → DA_FBX */
    base[3] = (int32_t)comp_value.y;          inv[3] = 0U;   /* iy → DA_FBY */

    for (uint8_t ch = 0; ch < AD_DA_CH_NUM; ch++)
    {
        const uint16_t *p_in  = in[ch];
        uint16_t       *p_out = out[ch];
        uint16_t        cnt   = n;
        const int32_t   b     = base[ch];

        if (inv[ch] != 0U)                    /* IN 通道: 反相 */
        {
            while (cnt >= 2U)                 /* 二重展开 */
            {
                p_out[0] = (uint16_t)__USAT(b - (int32_t)p_in[0], 12U);
                p_out[1] = (uint16_t)__USAT(b - (int32_t)p_in[1], 12U);
                p_in  += 2U;
                p_out += 2U;
                cnt   -= 2U;
            }
            if (cnt != 0U)                    /* 奇数尾巴(n=32 不会走到, 保通用性) */
            {
                *p_out = (uint16_t)__USAT(b - (int32_t)*p_in, 12U);
            }
        }
        else                                  /* FB 通道: 同相 */
        {
            while (cnt >= 2U)                 /* 二重展开 */
            {
                p_out[0] = (uint16_t)__USAT(b + (int32_t)p_in[0], 12U);
                p_out[1] = (uint16_t)__USAT(b + (int32_t)p_in[1], 12U);
                p_in  += 2U;
                p_out += 2U;
                cnt   -= 2U;
            }
            if (cnt != 0U)                    /* 奇数尾巴(n=32 不会走到, 保通用性) */
            {
                *p_out = (uint16_t)__USAT(b + (int32_t)*p_in, 12U);
            }
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

/* ------------------------------------------------------------------
 * AD-DA 处理通路初始化（时序见 spec §6：TX 先于 RX 启动、时钟最后释放）
 * ------------------------------------------------------------------ */

/* TX DMA 句柄(.ioc 无 DAC DMA, 手工创建), stm32g4xx_it.c 中 extern 引用 */
DMA_HandleTypeDef hdma_dac1_ch1;
DMA_HandleTypeDef hdma_dac1_ch2;
DMA_HandleTypeDef hdma_dac4_ch1;
DMA_HandleTypeDef hdma_dac4_ch2;

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

    /* 4. 初值 + tx_buf 整体预填(防前半块垃圾值):
          IN 通道(CH1) 2048 中点; FB 通道(CH2) 0 —— 静息 0V, 与算法/原仓库
          DAC_FBX_SET(0) 意图一致(spec §9.7) */
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0)    != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048) != HAL_OK) Error_Handler();
    if (HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0)    != HAL_OK) Error_Handler();
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

    /* 启动 4 路 TX DMA(循环、长度 AD_DA_BLOCK), 启动后即消费预填值 */
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)tx_buf[0], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, (uint32_t *)tx_buf[1], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
    if (HAL_DAC_Start_DMA(&hdac4, DAC_CHANNEL_1, (uint32_t *)tx_buf[2], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
    if (HAL_DAC_Start_DMA(&hdac4, DAC_CHANNEL_2, (uint32_t *)tx_buf[3], AD_DA_BLOCK, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();

    /* DAC 欠载兜底中断: HAL_DAC_Start_DMA 已使能 DMAUDRIE, 此处释放 NVIC。
       TIM6_DAC 线挂 DAC1&DAC3、TIM7_DAC 线挂 DAC2&DAC4; 优先级 2
       (低于 RX 节拍 0 与 TX 传输错误 1), handler 见 stm32g4xx_it.c:
       清欠载标志并继续运行, 严禁落入 Default_Handler 死锁 */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 2, 0);
    HAL_NVIC_SetPriority(TIM7_DAC_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    HAL_NVIC_EnableIRQ(TIM7_DAC_IRQn);

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


void ad5290_set_init(void)
{
  	AD5290_Init();
	HAL_Delay(10);
}


void param_init(void)
{
	if (bsp_flash_load(&flash_store) == 0)
	{
		radc_value  = flash_store.radc;
		comp_value  = flash_store.comp;
		LOG_SYS_INFO("load param from flash");
	}
	else
	{
		radc_value.x1 = 40;
		radc_value.x2 = 80;
		radc_value.x3 = 45;
		radc_value.y1 = 40;
		radc_value.y2 = 80;
		radc_value.y3 = 45;

		comp_value.x  = -80;
		comp_value.y  = -80;
		LOG_SYS_INFO("load param from default");
	}

	AD5290_SetAllCode((const uint8_t *)&radc_value);
	LOG_SYS_INFO("param: x1 = %04d, x2 = %04d, x3 = %04d, y1 = %04d, y2 = %04d, y3 = %04d", 
					radc_value.x1, radc_value.x2, radc_value.x3, radc_value.y1, radc_value.y2, radc_value.y3);
	LOG_SYS_INFO("comp: x = %04d, y = %04d", comp_value.x, comp_value.y);
	LOG_SYS_INFO("===================================================");


	for (uint8_t i = 0; i < 6; i++)
	{
		LED_USB_TOGGLE();
		HAL_Delay(150);
	}
}


void lsnet_init(void)
{
	ls_app_init();
}


/*    滑动平均滤波    */
#define ADC_FILTER_SIZE 4
static uint16_t adc_value_filtered[ADC_FILTER_SIZE] = {0};
static uint8_t adc_value_filtered_index = 0;
uint16_t adc_filter(uint16_t value)
{
	adc_value_filtered[adc_value_filtered_index] = value;
	adc_value_filtered_index = (adc_value_filtered_index + 1) % ADC_FILTER_SIZE;
	uint32_t sum = 0;
	for(uint8_t i = 0; i < ADC_FILTER_SIZE; i++)
	{
		sum += adc_value_filtered[i];
	}
	return sum / ADC_FILTER_SIZE;
}

/*****************************************************/

/*  定时器任务  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim)
{
	static uint8_t count = 0;
	if(htim->Instance == TIM2)
	{
		count = !count;
		if(count)
		{
			DAC_INX_SET(4095-2000);
		}
		else
		{
			DAC_INX_SET(0+2000);
		}
	}
}



/* file end */