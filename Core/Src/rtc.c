/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
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
#include "rtc.h"

/* USER CODE BEGIN 0 */

/* RTC 初始化错误码（防卡死调试用，rtc_init_err!=0 时系统照常启动，日志页可查） */
/* 0=成功  1=Init失败(多为LSE起振失败)  2=SetTime失败  3=SetDate失败  4=WakeUp失败 */
uint8_t rtc_init_err = 0;

/* 软件秒计数（F4 RTC 无秒寄存器，秒由 SSR 亚秒派生，标准做法=WUT 1秒中断累加） */
volatile uint32_t rtc_sec = 0;

/* WUT 唤醒中断回调（每 1 秒触发一次，软件秒+1；覆盖 HAL 弱回调） */
void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hhrtc)
{
    (void)hhrtc;
    rtc_sec++;
}

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    rtc_init_err = 1;   /* 卡住真凶：LSE 没起振/时钟没就绪，HAL_RTC_Init 超时失败 */
    return;             /* 不卡死：跳过下面所有 RTC 步骤，系统继续启动 */
  }

  /* ===== 终极修复：强制重写预分频（防止备份域旧标志导致 HAL_RTC_Init 跳过 PRER 配置） ===== */
  __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
  SET_BIT(hrtc.Instance->ISR, RTC_ISR_INIT);
  uint32_t rtc_tick = HAL_GetTick();
  while ((hrtc.Instance->ISR & RTC_ISR_INITF) == 0)
  {
      if (HAL_GetTick() - rtc_tick > 100) break;
  }
  hrtc.Instance->PRER = 255u;
  hrtc.Instance->PRER |= (127u << 16u);
  CLEAR_BIT(hrtc.Instance->ISR, RTC_ISR_INIT);
  rtc_tick = HAL_GetTick();
  while ((hrtc.Instance->ISR & RTC_ISR_INITF) != 0)
  {
      if (HAL_GetTick() - rtc_tick > 100) break;
  }
  __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);
  rtc_init_err = 0;

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    rtc_init_err = 2;
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    rtc_init_err = 3;
  }

  /** Enable the WakeUp
  */
  if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 0, RTC_WAKEUPCLOCK_CK_SPRE_16BITS) != HAL_OK)  /* 1秒唤醒（CK_SPRE=1Hz，Counter=0 → 每1秒中断） */
  {
    rtc_init_err = 4;
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

    /* ===== 修复：访问 RTC 备份域必须先使能 PWR 时钟 + 打开备份域写保护（STM32F4 硬性要求，CubeMX 标准模板） ===== */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    /* ===== 修复：使能 LSE 晶振。原实现漏了 HAL_RCC_OscConfig，LSE 未启动导致 HAL_RTC_Init 超时 ===== */
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE; /* 恢复 LSE */
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    for (uint8_t lse_try = 0; lse_try < 3; lse_try++)   /* LSE 起振慢，重试 3 次 */
    {
        if (HAL_RCC_OscConfig(&RCC_OscInitStruct) == HAL_OK) break;
        HAL_Delay(100);
        if (lse_try == 2) return;   /* 3次都失败：放弃，让 HAL_RTC_Init 超时走 rtc_init_err=1，不卡死 */
    }

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI; /* 占位：下面手动写HSE/32 */ /* 硬件裁决：HSE时钟源，秒会快24倍(走=硬件好) */
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* 硬件裁决：手动写 HSE/32 作 RTC 时钟源（RTCSEL=11） */
    CLEAR_BIT(RCC->BDCR, RCC_BDCR_RTCSEL);
    SET_BIT(RCC->BDCR, RCC_BDCR_RTCSEL_0 | RCC_BDCR_RTCSEL_1);
    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();

    /* RTC interrupt Init */
    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();

    /* RTC interrupt Deinit */
    HAL_NVIC_DisableIRQ(RTC_WKUP_IRQn);
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
