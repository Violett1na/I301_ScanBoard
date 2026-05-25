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
#define X_SDA3_Pin GPIO_PIN_4
#define X_SDA3_GPIO_Port GPIOC
#define X_SCL3_Pin GPIO_PIN_5
#define X_SCL3_GPIO_Port GPIOC
#define Y_SCL1_Pin GPIO_PIN_7
#define Y_SCL1_GPIO_Port GPIOC
#define Y_SDA1_Pin GPIO_PIN_8
#define Y_SDA1_GPIO_Port GPIOC
#define Y_SCL2_Pin GPIO_PIN_9
#define Y_SCL2_GPIO_Port GPIOC
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
