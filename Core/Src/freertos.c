/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include <stdio.h>   /* printf 声明（ printf 已静默，仅保留声明消除隐式警告） */
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Taskhandel.h"   /* 五任务函数声明（UI/Modbus/AT/MQTT/Dog_task） */
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
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
	/* ===== 创建五任务 =====
	   xTaskCreate 6个参数：
	   ① 任务函数  ② 任务名(调试用)  ③ 栈大小(单位word, 1word=4字节)
	   ④ 传给任务的参数  ⑤ 优先级  ⑥ 任务句柄(以后操作这个任务用)   */

	/* UI 任务(LVGL)：栈必须大(1024word=4KB)，LVGL 很吃栈 */
	xTaskCreate(UI,       "UITask",     1024, NULL, osPriorityAboveNormal,   NULL);

	/* Modbus 传感器轮询任务 */
	/* Sensor 优先级提到 AboveNormal+1：收帧期间只有 Dog 能抢（Dog 3s 才动一次），
	   不会被 UI 的 LVGL 渲染抢占导致丢字节（F411 串口无 FIFO） */
	xTaskCreate(Modbus,   "SensorTask",  1536, NULL, osPriorityAboveNormal+1, NULL);

	/* 网络 AT 任务(ESP8266 心跳) */
	xTaskCreate(AT,       "NetworkTask", 512, NULL, osPriorityNormal,        NULL);

	/* MQTT 任务(消费队列+上报) */
	xTaskCreate(MQTT,     "MQTTTask",   1536, NULL, osPriorityNormal,        NULL);   /* 512->1536，FlashCache_Write+sprintf 浮点栈溢出 HardFault（照 Modbus 教训） */

	/* 看门狗任务：优先级最高(AboveNormal+2)，3秒喂一次狗 */
	xTaskCreate(Dog_task, "Dog_task",    512, NULL, osPriorityAboveNormal+2, NULL);

	/* 创建完五任务，把 defaultTask 自己删掉，不再占用资源 */
	vTaskDelete(NULL);

	/* 下面永远不会执行 */
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* ===== FreeRTOS 故障钩子（堆不足/栈溢出不再静默） ===== */

/* 任务/队列创建失败会进这里：heap(ucHeap) 不够用。打印提示后死循环（喂狗停 -> IWDG 复位） */
void vApplicationMallocFailedHook(void)
{
    printf("[RTOS] Malloc Failed! FreeRTOS heap 不够，请加大 configTOTAL_HEAP_SIZE\r\n");
    for(;;) { }
}

/* 任务栈溢出会进这里：参数给出溢出的任务名，方便定位是哪个任务栈太小 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    printf("[RTOS] Stack Overflow! 任务栈溢出: %s\r\n", pcTaskName);
    for(;;) { }
}

/* USER CODE END Application */

