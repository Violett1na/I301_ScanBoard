#ifndef __TASK_H__
#define __TASK_H__

#include "main.h"
#include "adc.h"
#include "dac.h"
#include "tim.h"
#include "dac.h"
#include "opamp.h"
#include "ad5290.h"
#include "ls_proto_device_app.h"

#define DAC_INX_SET(val)    (hdac1.Instance->DHR12R1 = (val))
#define DAC_FBX_SET(val)    (hdac1.Instance->DHR12R2 = (val))
#define DAC_INY_SET(val)    (hdac4.Instance->DHR12R1 = (val))
#define DAC_FBY_SET(val)    (hdac4.Instance->DHR12R2 = (val))

#pragma pack(1)
typedef struct
{
    uint16_t x;
    uint16_t y;
} adc_value_fb_t;

typedef struct
{
    uint16_t vx;
    uint16_t vy;
    uint16_t ix;
    uint16_t iy;

    adc_value_fb_t fb;
} adc_value_t;


typedef struct
{
    uint8_t x1;
    uint8_t x2;
    uint8_t x3;
    uint8_t y1;
    uint8_t y2;
    uint8_t y3;
} radc_value_t;

#pragma pack()



extern volatile adc_value_t adc_value;

extern int16_t dac_offset_x;
extern int16_t dac_offset_y;
extern radc_value_t radc_value;

void AD_DA_Init(void);
void ad5290_set_init(void);
void lsnet_init(void);

uint16_t adc_filter(uint16_t value);

#endif
