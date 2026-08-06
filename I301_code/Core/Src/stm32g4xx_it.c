/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32g4xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "main.h"
#include "stm32g4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usb_device.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_adc1;
extern DMA_HandleTypeDef hdma_adc2;
extern DMA_HandleTypeDef hdma_adc3;
extern DMA_HandleTypeDef hdma_adc4;
extern DMA_HandleTypeDef hdma_adc5;
extern TIM_HandleTypeDef htim2;
/* USER CODE BEGIN EV */
extern PCD_HandleTypeDef hpcd_USB_FS;
extern DMA_HandleTypeDef hdma_dac1_ch1;
extern DMA_HandleTypeDef hdma_dac1_ch2;
extern DMA_HandleTypeDef hdma_dac4_ch1;
extern DMA_HandleTypeDef hdma_dac4_ch2;
extern volatile uint32_t ad_da_underrun_cnt;
extern volatile uint32_t ad_da_underrun_dac1;
extern volatile uint32_t ad_da_underrun_dac4;
extern volatile uint32_t ad_da_tx_tc_cnt[4];    /* AD_DA_CH_NUM, 避免在 it.c 引入 task.h */
extern void ad_da_te_latch(uint8_t ch, DMA_Channel_TypeDef *dma_ch);  /* TE 现场锁存(task.c) */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32G4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32g4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles DMA1 channel1 global interrupt.
  */
void DMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel1_IRQn 0 */

  /* USER CODE END DMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc3);
  /* USER CODE BEGIN DMA1_Channel1_IRQn 1 */

  /* USER CODE END DMA1_Channel1_IRQn 1 */
}

/**
  * @brief This function handles DMA1 channel2 global interrupt.
  */
void DMA1_Channel2_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel2_IRQn 0 */

  /* USER CODE END DMA1_Channel2_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc2);
  /* USER CODE BEGIN DMA1_Channel2_IRQn 1 */

  /* USER CODE END DMA1_Channel2_IRQn 1 */
}

/**
  * @brief This function handles DMA1 channel3 global interrupt.
  */
void DMA1_Channel3_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel3_IRQn 0 */

  /* USER CODE END DMA1_Channel3_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc5);
  /* USER CODE BEGIN DMA1_Channel3_IRQn 1 */

  /* USER CODE END DMA1_Channel3_IRQn 1 */
}

/**
  * @brief This function handles DMA1 channel4 global interrupt.
  */
void DMA1_Channel4_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel4_IRQn 0 */

  /* USER CODE END DMA1_Channel4_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc4);
  /* USER CODE BEGIN DMA1_Channel4_IRQn 1 */

  /* USER CODE END DMA1_Channel4_IRQn 1 */
}

/**
  * @brief This function handles TIM2 global interrupt.
  */
void TIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM2_IRQn 0 */

  /* USER CODE END TIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim2);
  /* USER CODE BEGIN TIM2_IRQn 1 */

  /* USER CODE END TIM2_IRQn 1 */
}

/**
  * @brief This function handles DMA2 channel1 global interrupt.
  */
void DMA2_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Channel1_IRQn 0 */

  /* USER CODE END DMA2_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc1);
  /* USER CODE BEGIN DMA2_Channel1_IRQn 1 */

  /* USER CODE END DMA2_Channel1_IRQn 1 */
}

/* USER CODE BEGIN 1 */
void USB_LP_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_FS);
}

/* TX DMA 中断: HT 已在 AD_DA_Init 屏蔽; 【诊断阶段】TC 解蔽用于计数实际搬运速率
   (理论 15625 Hz/路), 诊断结束后还原为 HAL_DMA_IRQHandler + 全屏蔽 */
void DMA1_Channel5_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_TCIF5)
    {
        DMA1->IFCR = DMA_IFCR_CTCIF5;
        ad_da_tx_tc_cnt[0]++;
    }
    if (DMA1->ISR & DMA_ISR_TEIF5)
    {
        ad_da_te_latch(0U, DMA1_Channel5);      /* 先抢现场, HAL 会清标志/关通道 */
        HAL_DMA_IRQHandler(&hdma_dac1_ch1);
    }
}

void DMA1_Channel6_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_TCIF6)
    {
        DMA1->IFCR = DMA_IFCR_CTCIF6;
        ad_da_tx_tc_cnt[1]++;
    }
    if (DMA1->ISR & DMA_ISR_TEIF6)
    {
        ad_da_te_latch(1U, DMA1_Channel6);      /* 先抢现场, HAL 会清标志/关通道 */
        HAL_DMA_IRQHandler(&hdma_dac1_ch2);
    }
}

void DMA1_Channel7_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_TCIF7)
    {
        DMA1->IFCR = DMA_IFCR_CTCIF7;
        ad_da_tx_tc_cnt[2]++;
    }
    if (DMA1->ISR & DMA_ISR_TEIF7)
    {
        ad_da_te_latch(2U, DMA1_Channel7);      /* 先抢现场, HAL 会清标志/关通道 */
        HAL_DMA_IRQHandler(&hdma_dac4_ch1);
    }
}

void DMA1_Channel8_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_TCIF8)
    {
        DMA1->IFCR = DMA_IFCR_CTCIF8;
        ad_da_tx_tc_cnt[3]++;
    }
    if (DMA1->ISR & DMA_ISR_TEIF8)
    {
        ad_da_te_latch(3U, DMA1_Channel8);      /* 先抢现场, HAL 会清标志/关通道 */
        HAL_DMA_IRQHandler(&hdma_dac4_ch2);
    }
}

/* DAC 欠载中断兜底: TIM6_DAC 线挂 DAC1&DAC3、TIM7_DAC 线挂 DAC2&DAC4(本工程用 DAC1/DAC4)。
   HAL_DAC_Start_DMA 已使能 DMAUDRIE, NVIC 在 AD_DA_Init 中释放;
   此处清欠载标志(写1清零)并继续运行——输出保持设计本就容忍偶发丢拍,
   严禁因无 handler 落入 Default_Handler(B .)而整机死锁。 */
void TIM6_DAC_IRQHandler(void)
{
    if (DAC1->SR & (DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2))
    {
        ad_da_underrun_cnt++;
        ad_da_underrun_dac1++;
        DAC1->SR = DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2;
    }
}

void TIM7_DAC_IRQHandler(void)
{
    if (DAC4->SR & (DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2))
    {
        ad_da_underrun_cnt++;
        ad_da_underrun_dac4++;
        DAC4->SR = DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2;
    }
}
/* USER CODE END 1 */
