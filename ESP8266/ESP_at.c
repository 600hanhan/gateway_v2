#include "esp_at.h"
#include "command.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * AT 指令层（基于 USART6）
 * ESP8266 接到 USART6（PC6=TX, PC7=RX），波特率 115200
 * ============================================================ */

/**
 * @brief 通用 AT 指令发送并逐行解析应答
 * @param cmd          要发送的 AT 指令（必须包含 \r\n）
 * @param expected_ack 期望出现的应答关键词，如 "OK"、"CONNECT"、"WIFI GOT IP"
 * @param timeout_ms   超时时间（毫秒）
 * @return ESP_OK(成功) / ESP_TIMEOUT(超时) / ESP_ERROR(收到 ERROR)
 *
 * 流程：清空环形缓冲 -> 发 AT 指令 -> 逐字节读应答 -> 按行解析 -> 匹配关键词
 */
ESP_Status_t ESP_SendCmd(const char *cmd, const char *expected_ack, uint32_t timeout_ms)
{
    /* 1. 清空环形缓冲区，丢弃旧数据 */
    {
        uint8_t dummy;
        while (RingBuf_Read(&dummy));    /* 逻辑清空：把读指针移到写指针 */
    }

    /* 2. 发送指令（ESP8266 uart6 波特率 115200） */
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);

    /* 3. 在超时时间内逐字节读取，按行解析 */
    char line[512];                      /* 一行数据缓冲（AT 应答最长一行的上限） */
    uint16_t line_pos = 0;
    uint32_t start_tick = HAL_GetTick();

    while ((HAL_GetTick() - start_tick) < timeout_ms)
    {
        uint8_t ch;
        while (RingBuf_Read(&ch))
        {
            start_tick = HAL_GetTick();  /* 关键：每收到一个字节就重置超时起点 */

            if (ch == '\n')              /* 一行结束 */
            {
                line[line_pos] = '\0';

                /* 检查是否包含期望应答 */
                if (expected_ack != NULL && strstr(line, expected_ack) != NULL)
                    return ESP_OK;

                /* 检查是否出错 */
                if (strstr(line, "ERROR") != NULL)
                    return ESP_ERROR;

                line_pos = 0;            /* 清空 line，准备读下一行 */
            }
            else if (ch != '\r')
            {
                if (line_pos < sizeof(line) - 1)
                    line[line_pos++] = ch;   /* 非回车字符追加到 line */
            }
        }
    }
    return ESP_TIMEOUT;
}

/* 简化版：发指令，只等待 "OK" */
ESP_Status_t ESP_SendCmd_OK(const char *cmd, uint32_t timeout_ms)
{
    return ESP_SendCmd(cmd, "OK", timeout_ms);
}
