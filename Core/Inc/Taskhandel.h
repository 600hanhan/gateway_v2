#ifndef TASKHANDEL_H
#define TASKHANDEL_H

#include "stdint.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

/* ===== 温湿度光照数据结构体 =====
   temp  : 温度（℃）
   shi   : 湿度（%RH）
   light : 光照（lux） */
typedef struct {
    float temp;
    float shi;
    float light;
} Data_t;

/* ===== 任务间同步对象：在 main.c 创建，各任务 extern 引用 ===== */
extern QueueHandle_t data_queue;        /* 传感器数据队列：SensorTask 生产，MQTTTask 消费 */
extern QueueHandle_t lvgl_data_queue;   /* 显示数据队列：MQTTTask 生产，UITask 消费 */
extern SemaphoreHandle_t uart1_mutex;   /* USART1 互斥量：保护 ESP8266 串口 */
extern SemaphoreHandle_t flash_mutex;   /* SPI Flash 互斥量：保护 Flash 读写 */

/* ===== 五任务函数声明（实现都在 Taskhandel.c） ===== */
void UI(void *argument);        /* UI 任务：LVGL 界面（填充） */
void Modbus(void *argument);    /* 传感器轮询任务（填充） */
void AT(void *argument);        /* 网络 AT 任务（填充） */
void MQTT(void *argument);      /* MQTT 上报任务（填充） */
void Dog_task(void *argument);  /* 看门狗喂狗任务（最高优先级） */

/* ===== 网络状态全局变量（pal.c/AT任务/MQTT任务 共用） ===== */
extern volatile uint8_t g_mqtt_connected;   /* 0=离线 1=在线（pal.c 连上后置 1） */
extern volatile uint8_t connect;            /* 0=正常 1=刚重连成功（防误判断网） */

#endif /* TASKHANDEL_H */
