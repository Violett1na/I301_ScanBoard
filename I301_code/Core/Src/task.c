#include "task.h"


adc_value_t adc_value;

void AD_DA_Init(void)
{
	/*执行ADC偏移校准*/
	ADC_Offset_Calibration(&hadc2);
	ADC_Offset_Calibration(&hadc3);
	ADC_Offset_Calibration(&hadc4);
	ADC_Offset_Calibration(&hadc5);
	HAL_Delay(10);
	/*启动DAC及对应的跟随器*/
	// HAL_DAC_Start(&hdac3, DAC_CHANNEL_1);
	// HAL_DAC_Start(&hdac3, DAC_CHANNEL_2);
	// HAL_DAC_Start(&hdac4, DAC_CHANNEL_1);
	// HAL_OPAMP_Start(&hopamp1);
	// HAL_OPAMP_Start(&hopamp3);
	// HAL_OPAMP_Start(&hopamp4);
	/*设置DAC初始默认输出值*/
    // hdac3.Instance->DHR12R1 = 0;
    // hdac3.Instance->DHR12R2 = 0;
	// hdac4.Instance->DHR12R1 = 0;
	/*启动ADC的DMA传输*/
	HAL_ADC_Start_DMA(&hadc2, (uint32_t*)&adc_value.ix, 1);
	HAL_ADC_Start_DMA(&hadc3, (uint32_t*)&adc_value.vx, 1);
	HAL_ADC_Start_DMA(&hadc4, (uint32_t*)&adc_value.vy, 1);
	HAL_ADC_Start_DMA(&hadc5, (uint32_t*)&adc_value.iy, 1);
	HAL_Delay(10);
	/*启动触发ADC的定时器*/
	HAL_TIM_Base_Start(&htim3);
}








/* file end */