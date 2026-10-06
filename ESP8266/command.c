#include "command.h"
#include "usart.h"
#include "stm32f4xx.h"
#include "dma.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * ESP8266 接收环形缓冲区（USART6 DMA 接收）
 * 生产者 = USART1 DMA 空闲中断（HAL_UARTEx_RxEventCallback）
 * 消费者 = AT 任务（RingBuf_Read / pal_tcp_recv_raw）
 * ============================================================ */

extern uint8_t a[256];                        /* DMA 接收缓冲（定义在 main.c） */
extern DMA_HandleTypeDef hdma_usart1_rx;      /* USART1 RX DMA（usart.c MspInit 配置） */

/* 环形缓冲区本体 */
uint8_t buffer[BUFFER_SIZE];
/* 读索引（任务消费时移动） */
uint8_t readIndex = 0;
/* 写索引（中断写入时移动） */
uint8_t writeIndex = 0;

/**
 * @brief  清空环形缓冲区（重置读写指针）
 */
void RingBuf_Clear(void)
{
    readIndex = 0;
    writeIndex = 0;
}

/**
 * @brief 增加读索引
 * @param length 要增加的长度
 */
void Command_AddReadIndex(uint8_t length)
{
    readIndex += length;
    readIndex %= BUFFER_SIZE;
}

/**
 * @brief 读取第 i 位数据（超过缓存区长度自动循环）
 */
uint8_t Command_Read(uint8_t i)
{
    uint8_t index = i % BUFFER_SIZE;
    return buffer[index];
}

/**
 * @brief 从缓冲区读出一个字节
 * @return 1=读到, 0=缓冲区空
 */
uint8_t RingBuf_Read(uint8_t *out)
{
    if (readIndex == writeIndex)
        return 0;                    /* 缓冲区空 */
    *out = buffer[readIndex];
    readIndex = (readIndex + 1) % BUFFER_SIZE;
    return 1;
}

/**
 * @brief 获取缓冲区未读数据长度
 */
uint8_t Command_GetLength(void)
{
    return (writeIndex + BUFFER_SIZE - readIndex) % BUFFER_SIZE;
}

/**
 * @brief 计算缓冲区剩余空间
 */
uint8_t Command_GetRemain(void)
{
    return BUFFER_SIZE - Command_GetLength();
}

/**
 * @brief 向缓冲区写入数据（满则停止写入，不覆盖旧数据）
 * @note  在 DMA 空闲中断回调里被调用，中断上下文单写单读无需锁
 */
uint8_t Command_Write(uint8_t *data, uint8_t len)
{
    if (data == NULL || len == 0)
        return 0;

    uint16_t remain = Command_GetRemain();
    if (remain < len)
        return 0;

    uint16_t idx = writeIndex;
    uint16_t part1 = BUFFER_SIZE - idx;          /* 写索引到末尾的剩余长度 */
    if (len <= part1)
    {
        memcpy(buffer + idx, data, len);
        writeIndex = (idx + len) % BUFFER_SIZE;
    }
    else                                         /* 绕回到开头（前面数据已被消费） */
    {
        memcpy(buffer + idx, data, part1);
        memcpy(buffer, data + part1, len - part1);
        writeIndex = (len - part1) % BUFFER_SIZE;
    }
    return len;
}

/**
 * @brief  USART1 DMA 空闲接收完成回调
 *         ESP8266 回包收完后进入，把 DMA 缓冲里的数据搬进环形缓冲区
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == &huart1)
    {
        Command_Write(a, Size);                   /* 向环形缓冲区写入数据 */

        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, a, sizeof(a));   /* 重新启动 DMA 空闲接收 */
        __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);      /* 禁用半传输中断 */
    }
}
