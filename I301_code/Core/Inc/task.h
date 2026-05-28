#ifndef __TASK_H__
#define __TASK_H__

#include "main.h"
#include "adc.h"
#include "dac.h"
#include "tim.h"
#include "dac.h"
#include "opamp.h"

#define DAC_INX_SET(val)    (hdac1.Instance->DHR12R1 = (val))
#define DAC_FBX_SET(val)    (hdac1.Instance->DHR12R2 = (val))
#define DAC_INY_SET(val)    (hdac4.Instance->DHR12R1 = (val))
#define DAC_FBY_SET(val)    (hdac4.Instance->DHR12R2 = (val))
typedef struct
{
    uint16_t vx;
    uint16_t vy;
    uint16_t ix;
    uint16_t iy;

} adc_value_t;


extern volatile adc_value_t adc_value;


void AD_DA_Init(void);


#endif
