#ifndef __TASK_H__
#define __TASK_H__

#include "main.h"
#include "adc.h"
#include "dac.h"
#include "tim.h"



typedef struct
{
    uint16_t vx;
    uint16_t vy;
    uint16_t ix;
    uint16_t iy;

} adc_value_t;


extern adc_value_t adc_value;


void AD_DA_Init(void);


#endif
