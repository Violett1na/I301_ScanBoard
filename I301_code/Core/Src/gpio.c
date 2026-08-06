/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, X_SCL1_Pin|X_SCL2_Pin|X_SCL3_Pin|Y_SCL1_Pin
                          |Y_SCL2_Pin|Y_SCL3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, X_SDA1_Pin|X_SDA2_Pin|X_SDA3_Pin|CS_Pin
                          |Y_SDA1_Pin|Y_SDA2_Pin|Y_SDA3_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED_USB_Pin|LED_X_Pin|LED_Y_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level : CH_FBX(PB3) CH_FBY(PB4) 默认低电平
    (4053 选 X0/Y0 = driver FB 送往 JB3/JB4, 与原仓库一致; 严禁悬空,
    否则反馈出口选通状态不定, JB3/JB4 波形异常) */
  HAL_GPIO_WritePin(GPIOB, CH_FBX_Pin|CH_FBY_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : X_SCL1_Pin X_SDA1_Pin X_SDA2_Pin X_SCL2_Pin
                           X_SDA3_Pin X_SCL3_Pin CS_Pin Y_SCL1_Pin
                           Y_SDA1_Pin Y_SCL2_Pin Y_SDA2_Pin Y_SCL3_Pin
                           Y_SDA3_Pin */
  GPIO_InitStruct.Pin = X_SCL1_Pin|X_SDA1_Pin|X_SDA2_Pin|X_SCL2_Pin
                          |X_SDA3_Pin|X_SCL3_Pin|CS_Pin|Y_SCL1_Pin
                          |Y_SDA1_Pin|Y_SCL2_Pin|Y_SDA2_Pin|Y_SCL3_Pin
                          |Y_SDA3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_USB_Pin LED_X_Pin LED_Y_Pin */
  GPIO_InitStruct.Pin = LED_USB_Pin|LED_X_Pin|LED_Y_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : CH_FBX_Pin(PB3) CH_FBY_Pin(PB4) 推挽/下拉/低速
    (74HC4053 反馈出口选择脚, 必须显式驱动, 不得悬空) */
  GPIO_InitStruct.Pin   = CH_FBX_Pin|CH_FBY_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
