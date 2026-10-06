#ifndef __FLASH_H
#define __FLASH_H

/* ============================================================
 * flash.h — 日志分区 + 断网缓存分区 的上层接口
 * SPI Flash 上层接口（按 flash.c 整理）
 * 上层（Taskhandel / page_main）只调用本文件接口，不碰底层地址
 * ============================================================ */

#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"

/* ===================== 日志等级 ===================== */
typedef enum {
    LOG_INFO  = 0,   /* 普通信息 */
    LOG_WARN  = 1,   /* 警告 */
    LOG_ERROR = 2    /* 错误 */
} Log_Level;

/* ===================== 日志条目结构体（对齐后固定 128B） ===================== */
#define LOG_MAGIC        0x55AAu   /* 魔数：标记该条目为有效日志 */
#define LOG_CONTENT_LEN  119       /* 日志内容最大长度（magic4+ts4+level1+119 = 128） */

typedef struct {
    uint32_t magic;                    /* 魔数：有效日志标记 */
    uint32_t time_stamp;               /* 系统运行时间戳（HAL_GetTick，单位ms） */
    uint8_t  level;                    /* 日志等级：LOG_INFO/WARN/ERROR */
    char     content[LOG_CONTENT_LEN]; /* 日志文本 */
} Log_Obj;

#define LOG_OBJ_SIZE  128              /* 单条日志占用的 Flash 空间（结构体对齐后正好128B） */

/* ===================== Flash 8MB 分区规划 =====================
 * 0x000000 ~ 0x3FFFFF : 日志分区（4MB，循环覆盖写）
 * 0x400000 ~ 0x7FFFFF : 断网缓存分区（4MB，补传用）
 * ============================================================ */
#define LOG_PART_ADDR     0x000000u    /* 日志分区基地址 */
#define LOG_PART_SIZE     0x400000u    /* 日志分区大小 4MB */

#define CACHE_START_ADDR  0x400000u    /* 缓存分区基地址 4MB */
#define CACHE_END_ADDR    0x800000u    /* 缓存分区结束 8MB */
#define CACHE_SECTOR_SIZE 4096u        /* W25Q 扇区大小 4KB */
#define CACHE_ENTRY_SIZE  128u         /* 单条缓存条目大小 */
#define MAGIC_VALID       0x55AAu      /* 缓存条目有效魔数 */
#define MAGIC_EMPTY       0xFFFFu      /* 缓存条目清空标记（Flash 擦除态全 0xFF） */

/* ===================== 全局变量（Taskhandel.c 中定义，本文件声明） ===================== */
extern SemaphoreHandle_t flash_mutex;  /* Flash 互斥量：保护 Flash 读写 */
extern uint32_t log_write_offset;      /* 日志区写指针：下一条日志要写入的空白地址 */

/* ===================== 分区读写（上层统一入口） ===================== */
uint8_t Flash_Part_Read(uint32_t part_base, uint32_t offset, uint8_t *buf, uint16_t len);   /* 分区读，0=成功 */
uint8_t Flash_Part_Write(uint32_t part_base, uint32_t offset, uint8_t *buf, uint16_t len);  /* 分区写，0=成功 */

/* ===================== 日志 API（核心） ===================== */
void    Log_Write(Log_Level level, const char *str);                 /* 写一条日志（写满循环覆盖） */
uint8_t Log_ReadOne(uint32_t log_offset, Log_Obj *log_obj);          /* 读一条日志：0=有效 1=无效 */
void    Log_EraseAll(void);                                          /* 整分区擦除（开发期偶尔用） */

/* ===================== 断网缓存 API（补传用） ===================== */
void FlashCache_Init(void);                 /* 上电扫描缓存分区，定位续写地址 */
int  FlashCache_Write(const char *json, uint16_t len);               /* 写入一条缓存 */
int  FlashCache_Read(char *json, uint16_t *len);                     /* 读一条缓存（不销毁） */
int  FlashCache_ReadAndConsume(char *json, uint16_t *len);           /* 读一条缓存并销毁 */
int  FlashCache_HasData(void);                                       /* 是否有未补传数据 */
void FlashCache_Clear(void);                /* 清空整个缓存分区 */
void FlashCache_MarkSent(void);             /* 标记当前条已补传（只改魔数） */
void my_clear(void);                        /* 读写指针归零 */

/* ===================== MQTT 报文组帧（用） ===================== */
void MQTT_PublishJson(const char *json);

#endif /* __FLASH_H */
