#ifndef MODBUS_H
#define MODBUS_H
#include "stdint.h"
#include "stm32f4xx.h"

/* RS485 方向控制宏：PB0 高电平=发送，低电平=接收 */
#define RS485_TX  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET)
#define RS485_RX  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)

/* CRC16 校验：flag=1 按位循环法，flag=0 查表法（结果都是 Modbus CRC16） */
uint16_t CRC_16_MODBUS(uint8_t *data, uint8_t len, uint8_t flag);

/* 单从机配置结构体：地址/功能码/起始寄存器/寄存器个数/连续错误计数/数据1/数据2 */
typedef struct
{
    uint8_t  dev_addr;    /* 从机地址（Modbus 设备地址） */
    uint8_t  func;        /* 功能码（0x03=读保持寄存器） */
    uint16_t reg_start;   /* 起始寄存器地址 */
    uint16_t reg_num;     /* 要读的寄存器个数 */
    uint16_t err_cnt;     /* 连续失败次数（超限可临时跳过该从站） */
    float    val1;        /* 解析后的数据1 */
    float    val2;        /* 解析后的数据2 */
} ModDev_t;

extern ModDev_t dev_list[];   /* 多从站配置表（定义在 modbus.c） */
extern uint8_t dev_cnt;       /* 从站个数 */

/* 通用 Modbus 0x03 读寄存器（单从站单次） */
uint8_t Modbus_Read_Dev(uint8_t addr, uint8_t func, uint16_t reg, uint16_t regcnt, uint16_t *buf);

/* 整表轮询入口：遍历所有从站读一遍 */
void Modbus_Poll_AllDev(void);

/* 把 dev_list 的采集结果打包成 Data_t 写入 data_queue（供 MQTT 任务消费） */
void Modbus_Upload_Json(void);

/* MQTT 上报（先声明，Taskhandel.c 实现） */
void MQTT_SendAllDev_NoWait(void);

#endif
