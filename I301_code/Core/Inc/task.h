#ifndef __TASK_H__
#define __TASK_H__

#include "main.h"
#include "adc.h"
#include "dac.h"
#include "tim.h"
#include "dac.h"
#include "opamp.h"
#include "ad5290.h"
#include "gpio.h"
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

typedef struct
{
    int16_t x;
    int16_t y;
} comp_value_t;

/* Flash 持久化存储总结构体，所有需要保存到 Flash 的参数统一放入此处 */
typedef struct
{
    radc_value_t  radc;           /* 电位器码值 */
    comp_value_t  comp;           /* 补偿值 */
    
} flash_store_t;



#pragma pack()



extern volatile adc_value_t adc_value;

extern radc_value_t radc_value;
extern comp_value_t comp_value;
extern flash_store_t flash_store;

void AD_DA_Init(void);
void ad5290_set_init(void);
void param_init(void);
void lsnet_init(void);

uint16_t adc_filter(uint16_t value);

#endif
