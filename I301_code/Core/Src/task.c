#include "task.h"


volatile adc_value_t adc_value;

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
    DAC_INX_SET(0);
    DAC_FBX_SET(0);	
    DAC_INY_SET(0);
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
	// HAL_TIM_Base_Start(&htim2);
}


void ad5290_set_init(void)
{
  	AD5290_Init();

	
}
/*****************************************************/

/*  定时器任务  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim)
{
	if(htim->Instance == TIM2)
	{
		DAC_FBX_SET(2048);
		DAC_FBY_SET(2048);
	}
}



/* file end */