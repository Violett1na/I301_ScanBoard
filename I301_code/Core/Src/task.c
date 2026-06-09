#include "task.h"


volatile adc_value_t adc_value;

radc_value_t radc_value;
comp_value_t comp_value;

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
	radc_value.x1 = 7;
	radc_value.x2 = 113;
	radc_value.x3 = 75;
	radc_value.y1 = 100;
	radc_value.y2 = 100;
	radc_value.y3 = 100;

	AD5290_SetAllCode((const uint8_t *)&radc_value);
}


void param_init(void)
{
	comp_value.x = -80;
	comp_value.y = 0;
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