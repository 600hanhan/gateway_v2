/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "cmsis_os.h"
#include "dma.h"
#include "iwdg.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lvgl.h"   /* LVGL 图形库：TIM3 中断里调 lv_tick_inc */
#include <stdio.h>        /* printf 重定向需要 */
#include "queue.h"        /* FreeRTOS 队列 API */
#include "semphr.h"       /* FreeRTOS 互斥量 API */
#include "Taskhandel.h"   /* 五任务函数声明 + Data_t 结构体 */
#include "led.h"        /* PC13 状态灯 */
#include "modbus.h"      /* RS485_TX/RX 方向宏（PB0）， RS485 需要 */
#include "bsp_flash.h"  /* W25Q 底层驱动（ SPI Flash） */
#include "rtc.h"        /* 实时时钟 RTC */
#include "flash.h"      /* 日志分区/缓存分区上层接口 */
/* ===== disable semihosting: printf must use fputc, not BKPT 0xAB ===== */
#pragma import(__use_no_semihosting)
struct __FILE { int handle; };
FILE __stdout;
void _sys_exit(int x) { (void)x; for (;;) {} }
void _ttywrch(int ch) { (void)ch; }
/* ===== end semihosting guard ===== */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

uint8_t a[256];   /* ESP8266 DMA 接收缓冲 */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
	
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_IWDG_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  /* ===== 启动 ESP8266 DMA 空闲中断接收 ===== */
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, a, sizeof(a));
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);   /* 禁用半传输中断，只留空闲中断 */

  /* ===== SPI Flash 初始化 ===== */
  SPI_FLASH_Init();        /* 配置 CS(PB12) + 使能 SPI2 外设（外接 W25Q64） */
  FlashCache_Init();       /* 扫描缓存分区定位续写地址（补传用） */
  /* ===== 验收点：打印 Flash JEDEC ID（外接 W25Q64 标准 ID = 0xEF4017） ===== */
  printf("[Flash] JEDEC ID: 0x%06X\r\n", (unsigned int)SPI_FLASH_ReadID());
	/* 启动 TIM3 1ms 中断 = LVGL 心跳源 */
	HAL_TIM_Base_Start_IT(&htim3);
	printf("===== STM32F411 RTOS 工程已启动 =====\r\n");
	/* 实时时钟 RTC 初始化（MX_RTC_Init，LSE 时钟源） */
	MX_RTC_Init();

	printf("System Clock: %lu Hz\r\n", SystemCoreClock);
	printf("HAL TimeBase: TIM4\r\n");

	/* 启动独立看门狗：20s 内没人喂狗就硬件复位（喂狗在 Dog_task） */
	HAL_IWDG_Init(&hiwdg);

	/* 配置板载 PC13 为状态灯：正常常亮 / 离线灭 / 补传闪 */
	Led_Init();

	/* ===== RS485 方向引脚 PB0 初始化（，不依赖 CubeMX） =====
	   之前 CubeMX 里漏配了 PB0，RS485_TX/RX 宏写一个没初始化的引脚，
	   方向控制完全无效 -> 问询帧发不出去 -> 传感器永远不应答！
	   手动初始化：GPIOB 时钟 + PB0 推挽输出，默认切到接收态 */
	__HAL_RCC_GPIOB_CLK_ENABLE();
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin  = GPIO_PIN_0;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;   /* 推挽输出 */
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	RS485_RX;   /* 默认接收方向（PB0=0），要发数据时 Modbus 任务会切 TX */

	/* ===== 创建任务间同步对象 ===== */
	/* 队列容量=1，覆盖式：只保留最新一条数据（传感器场景只看最新值） */
	data_queue      = xQueueCreate(1, sizeof(Data_t));   /* 传感器数据：SensorTask -> MQTTTask */
	lvgl_data_queue = xQueueCreate(1, sizeof(Data_t));   /* 显示数据：MQTTTask -> UITask */

	/* 互斥量：谁上锁谁才能开锁，保护共享硬件资源 */
	uart1_mutex = xSemaphoreCreateMutex();   /* 保护 ESP8266 串口（AT 任务和 MQTT 任务共用） */
	flash_mutex = xSemaphoreCreateMutex();   /* 保护 SPI Flash（日志和缓存共用） */

	/* 验收演示（必须放在 flash_mutex 创建之后，否则 Log_Write 写不进去）：
	   烧录后触摸"日志"按钮，日志页第一条能看到 "RTC OK xx:xx:xx" = 实时时钟已跑起来 */
	RTC_TimeTypeDef rtc_t; RTC_DateTypeDef rtc_d;
	HAL_RTC_GetTime(&hrtc, &rtc_t, RTC_FORMAT_BCD);
	HAL_RTC_GetDate(&hrtc, &rtc_d, RTC_FORMAT_BCD);
	{ char rtc_log[32];
	  if (rtc_init_err == 0) sprintf(rtc_log, "RTC OK %02x:%02x:%02x", rtc_t.Hours, rtc_t.Minutes, rtc_t.Seconds);
	  else sprintf(rtc_log, "RTC FAIL %d", rtc_init_err);  /* 1=LSE起振失败 2/3/4=写时间失败 */
	  Log_Write(LOG_INFO, rtc_log); }
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 12;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* printf 重定向到 USART1（PA9 发出，串口助手 115200 查看日志）
   注意：Keil 里必须勾选 Use MicroLIB，否则 printf 走 semihosting 会卡死 */
int fputc(int ch, FILE *f)
{
    /* printf 静默（USART1 让给 ESP8266 使用）。
       串口助手接 PA9/PA10 看到的是 ESP8266 的 AT 指令和 MQTT 报文；
       MCU 状态看屏幕 + PC13 灯 + Flash 日志页。以后想恢复调试口再改回发送。 */
    (void)f;
    return ch;
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */
  if (htim == &htim3)      /* TIM3 = LVGL 1ms 心跳 */
  {
    lv_tick_inc(1);       /* 告诉 LVGL 又过去了 1 毫秒 */
  }
  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
