#ifndef __BSP_FLASH_H
#define __BSP_FLASH_H

/* ============================================================
 * bsp_flash.h — W25Q64 底层 SPI 驱动 接口
 * SPI Flash 底层驱动接口（按 bsp_flash.c 整理）
 * 引脚：SPI2 = PB13/SCK、PB14/MISO、PB15/MOSI，CS = PB12（外接 W25Q64）
 * 注：Flash 走 SPI2 与 LCD(SPI1)/触摸(SPI1) 总线分离，bsp_flash.c 内无需 SPI1 锁；
 *     SPI1_LOCK 宏保留给 LCD/触摸互斥用（touch.c/lcd.c/lv_port_disp.c）。
 * ============================================================ */

#include "stm32f4xx_hal.h"
#include <stdio.h>          /* FLASH_ERROR 里的 printf 需要 */
#include "FreeRTOS.h"       /* SPI1 总线互斥锁需要（修正） */
#include "semphr.h"
#include "task.h"

/* ===================== SPI2 句柄映射 =====================
 * bsp_flash.c 底层统一用 SpiHandle 读写 SPI2
 * 用户工程的 hspi2 由 CubeMX 在 spi.c 中定义，这里 extern + 宏映射 */
extern SPI_HandleTypeDef hspi2;
#define SpiHandle  hspi2

/* ===================== CS 片选引脚（PB12，bsp_flash.c 的 Init 里自管） ===================== */
#define FLASH_CS_GPIO_PORT   GPIOB
#define FLASH_CS_PIN         GPIO_PIN_12

/* ===================== SPI1 总线互斥锁（修正） =====================
 * 板载 Flash 与 LCD(CS=PB6)/触摸(CS=PB10) 分时共用 SPI1 总线：
 * LCD 刷屏、触摸采样、Flash 读写必须在同一时刻独占 SPI1，否则数据互相污染（花屏/日志错乱）。
 * flash_mutex 定义于 Taskhandel.c，创建于 main.c（已有，原本保护 Flash，现扩展为 SPI1 总线锁）。
 * main() 阶段（调度器未启动）不拿锁，任务阶段才互斥。 */
extern SemaphoreHandle_t flash_mutex;
#define SPI1_LOCK()   do{ if(flash_mutex!=NULL && xTaskGetSchedulerState()==taskSCHEDULER_RUNNING){ xSemaphoreTake(flash_mutex, portMAX_DELAY); } }while(0)
#define SPI1_UNLOCK() do{ if(flash_mutex!=NULL && xTaskGetSchedulerState()==taskSCHEDULER_RUNNING){ xSemaphoreGive(flash_mutex); } }while(0)

#define SPI_FLASH_CS_LOW()   HAL_GPIO_WritePin(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, GPIO_PIN_RESET)  /* 选中 Flash */
#define SPI_FLASH_CS_HIGH()  HAL_GPIO_WritePin(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, GPIO_PIN_SET)    /* 释放 Flash */

/* ===================== W25Q 指令集 ===================== */
#define W25X_WriteEnable      0x06   /* 写使能 */
#define W25X_WriteDisable     0x04   /* 写禁止 */
#define W25X_ReadStatusReg    0x05   /* 读状态寄存器1 */
#define W25X_WriteStatusReg   0x01   /* 写状态寄存器 */
#define W25X_ReadData         0x03   /* 读数据（普通读） */
#define W25X_FastReadData     0x0B   /* 快速读 */
#define W25X_PageProgram      0x02   /* 页编程（写） */
#define W25X_SectorErase      0x20   /* 扇区擦除（4KB） */
#define W25X_BlockErase       0xD8   /* 块擦除（64KB） */
#define W25X_ChipErase        0xC7   /* 整片擦除 */
#define W25X_PowerDown        0xB9   /* 掉电 */
#define W25X_ReleasePowerDown 0xAB   /* 释放掉电 / 读设备ID */
#define W25X_DeviceID         0xAB   /* 设备ID */
#define W25X_JedecDeviceID    0x9F   /* JEDEC ID（0xEF 4015 = W25Q64） */

/* ===================== 状态寄存器位 ===================== */
#define WIP_Flag  0x01               /* WIP：写忙标志位（bit0=1 忙） */

/* ===================== 页大小 / 超时参数 ===================== */
#define SPI_FLASH_PageSize         256    /* W25Q 一页 256 字节 */
#define SPI_FLASH_PerWritePageSize 256    /* 单次页编程最大 256 字节 */
#define Dummy_Byte                 0xFF   /* SPI 读时发送的哑字节 */

#define SPIT_LONG_TIMEOUT          0xFFFF  /* 长超时（擦除等） */
#define SPIT_FLAG_TIMEOUT          0x1000  /* 单字节超时计数 */

/* ===================== 调试输出宏（走 USART1 printf） ===================== */
#define FLASH_ERROR(fmt, ...)  printf("[FLASH] " fmt "\r\n", ##__VA_ARGS__)

/* ===================== 底层驱动 API ===================== */
void     SPI_FLASH_Init(void);                                   /* 初始化 CS + 使能 SPI1 */
void     SPI_FLASH_SectorErase(uint32_t SectorAddr);             /* 扇区擦除（4KB） */
void     SPI_FLASH_BulkErase(void);                              /* 整片擦除 */
void     SPI_FLASH_PageWrite(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);  /* 单页写（≤256B） */
void     SPI_FLASH_BufferWrite(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite); /* 自动分页批量写 */
void     SPI_FLASH_BufferRead(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);    /* 批量读 */
uint32_t SPI_FLASH_ReadID(void);                                 /* 读 JEDEC ID：W25Q64=0xEF4015 */
uint32_t SPI_FLASH_ReadDeviceID(void);                           /* 读设备 ID */
void     SPI_FLASH_StartReadSequence(uint32_t ReadAddr);         /* 开始连续读序列 */
uint8_t  SPI_FLASH_ReadByte(void);                               /* 连续读模式下读 1 字节 */
uint8_t  SPI_FLASH_SendByte(uint8_t byte);                       /* SPI 收发 1 字节 */
uint16_t SPI_FLASH_SendHalfWord(uint16_t HalfWord);              /* SPI 收发 1 半字 */
void     SPI_FLASH_WriteEnable(void);                            /* 写使能（写操作前必须） */
void     SPI_FLASH_WaitForWriteEnd(void);                        /* 等待 WIP 清除（写/擦完成） */
void     SPI_Flash_PowerDown(void);                              /* 掉电 */
void     SPI_Flash_WAKEUP(void);                                 /* 唤醒 */

#endif /* __BSP_FLASH_H */
