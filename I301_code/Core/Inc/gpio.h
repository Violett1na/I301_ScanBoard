/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.h
  * @brief   This file contains all the function prototypes for
  *          the gpio.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GPIO_H__
#define __GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

/* LED 低电平点亮，高电平熄灭 */

/* LED 点亮（拉低） */
#define LED_USB_ON()        HAL_GPIO_WritePin(LED_USB_GPIO_Port, LED_USB_Pin, GPIO_PIN_RESET)
#define LED_X_ON()          HAL_GPIO_WritePin(LED_X_GPIO_Port,   LED_X_Pin,   GPIO_PIN_RESET)
#define LED_Y_ON()          HAL_GPIO_WritePin(LED_Y_GPIO_Port,   LED_Y_Pin,   GPIO_PIN_RESET)

/* LED 熄灭（拉高） */
#define LED_USB_OFF()       HAL_GPIO_WritePin(LED_USB_GPIO_Port, LED_USB_Pin, GPIO_PIN_SET)
#define LED_X_OFF()         HAL_GPIO_WritePin(LED_X_GPIO_Port,   LED_X_Pin,   GPIO_PIN_SET)
#define LED_Y_OFF()         HAL_GPIO_WritePin(LED_Y_GPIO_Port,   LED_Y_Pin,   GPIO_PIN_SET)

/* LED 翻转 */
#define LED_USB_TOGGLE()    HAL_GPIO_TogglePin(LED_USB_GPIO_Port, LED_USB_Pin)
#define LED_X_TOGGLE()      HAL_GPIO_TogglePin(LED_X_GPIO_Port,   LED_X_Pin)
#define LED_Y_TOGGLE()      HAL_GPIO_TogglePin(LED_Y_GPIO_Port,   LED_Y_Pin)

/* LED 写入指定状态: 1=点亮(低), 0=熄灭(高) */
#define LED_USB_WRITE(on)   HAL_GPIO_WritePin(LED_USB_GPIO_Port, LED_USB_Pin, (on) ? GPIO_PIN_RESET : GPIO_PIN_SET)
#define LED_X_WRITE(on)     HAL_GPIO_WritePin(LED_X_GPIO_Port,   LED_X_Pin,   (on) ? GPIO_PIN_RESET : GPIO_PIN_SET)
#define LED_Y_WRITE(on)     HAL_GPIO_WritePin(LED_Y_GPIO_Port,   LED_Y_Pin,   (on) ? GPIO_PIN_RESET : GPIO_PIN_SET)

/* 全部 LED 操作 */
#define LED_ALL_ON()        do { LED_USB_ON();  LED_X_ON();  LED_Y_ON();  } while(0)
#define LED_ALL_OFF()       do { LED_USB_OFF(); LED_X_OFF(); LED_Y_OFF(); } while(0)
#define LED_ALL_TOGGLE()    do { LED_USB_TOGGLE(); LED_X_TOGGLE(); LED_Y_TOGGLE(); } while(0)

/* USER CODE END Private defines */

void MX_GPIO_Init(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__ GPIO_H__ */

