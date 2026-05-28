/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "mylog.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define X_SCL1_Pin GPIO_PIN_0
#define X_SCL1_GPIO_Port GPIOC
#define X_SDA1_Pin GPIO_PIN_1
#define X_SDA1_GPIO_Port GPIOC
#define X_SDA2_Pin GPIO_PIN_2
#define X_SDA2_GPIO_Port GPIOC
#define X_SCL2_Pin GPIO_PIN_3
#define X_SCL2_GPIO_Port GPIOC
#define ADC_IX_Pin GPIO_PIN_0
#define ADC_IX_GPIO_Port GPIOA
#define ADC_FBX_Pin GPIO_PIN_2
#define ADC_FBX_GPIO_Port GPIOA
#define ADC_FBY_Pin GPIO_PIN_3
#define ADC_FBY_GPIO_Port GPIOA
#define DA_INX_Pin GPIO_PIN_4
#define DA_INX_GPIO_Port GPIOA
#define DA_FBX_Pin GPIO_PIN_5
#define DA_FBX_GPIO_Port GPIOA
#define X_SDA3_Pin GPIO_PIN_4
#define X_SDA3_GPIO_Port GPIOC
#define X_SCL3_Pin GPIO_PIN_5
#define X_SCL3_GPIO_Port GPIOC
#define LED_USB_Pin GPIO_PIN_0
#define LED_USB_GPIO_Port GPIOB
#define LED_X_Pin GPIO_PIN_1
#define LED_X_GPIO_Port GPIOB
#define LED_Y_Pin GPIO_PIN_2
#define LED_Y_GPIO_Port GPIOB
#define DA_INY_Pin GPIO_PIN_12
#define DA_INY_GPIO_Port GPIOB
#define ADC_VX_Pin GPIO_PIN_13
#define ADC_VX_GPIO_Port GPIOB
#define ADC_VY_Pin GPIO_PIN_15
#define ADC_VY_GPIO_Port GPIOB
#define CS_Pin GPIO_PIN_6
#define CS_GPIO_Port GPIOC
#define Y_SCL1_Pin GPIO_PIN_7
#define Y_SCL1_GPIO_Port GPIOC
#define Y_SDA1_Pin GPIO_PIN_8
#define Y_SDA1_GPIO_Port GPIOC
#define Y_SCL2_Pin GPIO_PIN_9
#define Y_SCL2_GPIO_Port GPIOC
#define DA_FBY_Pin GPIO_PIN_8
#define DA_FBY_GPIO_Port GPIOA
#define ADC_IY_Pin GPIO_PIN_9
#define ADC_IY_GPIO_Port GPIOA
#define Y_SDA2_Pin GPIO_PIN_10
#define Y_SDA2_GPIO_Port GPIOC
#define Y_SCL3_Pin GPIO_PIN_11
#define Y_SCL3_GPIO_Port GPIOC
#define Y_SDA3_Pin GPIO_PIN_12
#define Y_SDA3_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
