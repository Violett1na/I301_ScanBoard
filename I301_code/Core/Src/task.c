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