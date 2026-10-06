#include "esp_at.h"
#include "command.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include "stdlib.h"
#include "pal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "flash.h"
#include "Taskhandel.h"

/* ============================================================
 * 协议栈层（基于 USART6，由 USART1 改接）
 * 注：libemqtt.h 未实际使用（MQTT 报文全是手工
 *     构造，没调用那个库），不移植它。
 * ============================================================ */

/* ESP8266 uart6 波特率 115200，接收走 command.c 的中断环形缓冲 */

/**
 * @brief MQTT 首次连接（开机调用一次）
 * 流程：+++退出透传 -> AT 测试 -> STA 模式 -> 连 WiFi ->
 *       TCP 连巴法云 9501 -> 进透传 -> 发 MQTT CONNECT -> 收 CONNACK
 */
void MQTT_Init(void)
{
    /* +++ 退出可能残留的透传模式（透传模式下 AT 指令不生效） */
    HAL_UART_Transmit(&huart1, (uint8_t *)"+++\r\n", 3, 100);
    vTaskDelay(1000);

    /* 任意合法 AT 指令，完成模式切换（不操作 TCP，安全无断连） */
    ESP_SendCmd_OK("AT\r\n", 2000);
    vTaskDelay(200);

    /* 设为 STA 模式（连路由器，不是热点） */
    ESP_SendCmd_OK("AT+CWMODE=1\r\n", 2000);

    /* 连接 WiFi，等拿到 IP 才算成功 */
    ESP_SendCmd("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASS "\"\r\n", "WIFI GOT IP", 5000);
    vTaskDelay(500);

    /* 建立 TCP 连接（巴法云 TCP 透传端口 9501） */
    ESP_SendCmd("AT+CIPSTART=\"TCP\",\"" BEMFA_BROKER "\"," BEMFA_PORT "\r\n", "CONNECT", 2000);
    vTaskDelay(500);

    /* 进入透传模式 */
    ESP_SendCmd_OK("AT+CIPMODE=1\r\n", 2000);
    /* 启动透传（应答 ">" 提示符） */
    ESP_SendCmd("AT+CIPSEND\r\n", ">", 5000);
    vTaskDelay(500);
    RingBuf_Clear();

    /* ===== 手工构造 MQTT CONNECT 报文（MQTT v3.1，QoS0） =====
       0x10 固定报头=CONNECT
       0x2E 剩余长度=46
       可变报头：协议名 MQIsdp(6) + 版本0x03 + 连接标志0x02(clean session) + 保活0x0078(120s)
       有效载荷：32字节 客户端ID（巴法云私钥做 clientID，服务器用它认人） */
    uint8_t connect_packet[] = {
        0x10, 0x2E,
        0x00, 0x06,
        0x4D, 0x51, 0x49, 0x73, 0x64, 0x70,    /* "MQIsdp" */
        0x03, 0x02, 0x00, 0x78,
        0x00, 0x20,                            /* 客户端ID长度=32 */
        '0','8','1','9','0','7','1','d',
        '7','c','f','0','1','6','1','f',
        '6','9','6','b','b','0','4','b',
        'f','1','b','6','3','9','e','c'
    };
    /* 透传模式下直接把报文写到 TCP 流 */
    HAL_UART_Transmit(&huart1, connect_packet, sizeof(connect_packet), 5000);
    vTaskDelay(300);

    /* 解析 CONNACK 应答（服务器回 4 字节 0x20 0x02 0x00 0x00 表示接受连接） */
    uint8_t connack[4];
    int ret = pal_tcp_recv_raw(0, connack, 4, 3000);
    if (ret == 4 && connack[0] == 0x20 && connack[1] == 0x02 && (connack[3] == 0x00 || connack[3] == 0x01))  /* 巴法云实测回 20 02 01 00（return code=0x01），只认 0x00 会误判失败 */
    {
        /* 连接成功后订阅主题（SUBSCRIBE 报文） */
        uint8_t sub_packet[] = {
            0x82, 0x0C,            /* 固定报头 SUBSCRIBE */
            0x00, 0x01,            /* 报文标识符 Packet ID = 1 */
            0x00, 0x07,            /* 主题名长度 = 7 */
            's','t','m','3','2','f','4',  /* 主题名：stm32f4 */
            0x01                   /* 订阅 QoS 等级 = 1 */
        };
        HAL_UART_Transmit(&huart1, sub_packet, sizeof(sub_packet), 5000);
        vTaskDelay(300);
        g_mqtt_connected = 1;      /* 全局标志置位：MQTT 任务看到它就走在线分支 */
        Log_Write(LOG_INFO, "CONNECT FINISH");
    }
    else
    {
        g_mqtt_connected = 0;      /* 连接失败，AT 任务每 10 秒走 RE_MQTT_Init 重试 */
    }
}

/**
 * @brief MQTT 断线重连（心跳失败 >=3 次后，AT 任务调用）
 */
void RE_MQTT_Init(void)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)"+++\r\n", 3, 100);
    vTaskDelay(1000);
    /* 软复位 ESP8266 两次，清掉卡死的网络状态 */
    for (int i = 0; i < 2; i++)
    {
        ESP_SendCmd_OK("AT+RST\r\n", 2000);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_SendCmd_OK("AT+CWMODE=1\r\n", 2000);
    ESP_SendCmd("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASS "\"\r\n", "WIFI GOT IP", 5000);

    /* 建立 TCP 连接 */
    ESP_SendCmd("AT+CIPSTART=\"TCP\",\"" BEMFA_BROKER "\"," BEMFA_PORT "\r\n", "CONNECT", 5000);

    ESP_SendCmd_OK("AT+CIPMODE=1\r\n", 2000);
    ESP_SendCmd("AT+CIPSEND\r\n", ">", 5000);
    vTaskDelay(500);
    RingBuf_Clear();

    /* MQTT 连接报文（同上） */
    uint8_t connect_packet[] = {
        0x10, 0x2E,
        0x00, 0x06,
        0x4D, 0x51, 0x49, 0x73, 0x64, 0x70,
        0x03, 0x02, 0x00, 0x78,
        0x00, 0x20,
        '0','8','1','9','0','7','1','d',
        '7','c','f','0','1','6','1','f',
        '6','9','6','b','b','0','4','b',
        'f','1','b','6','3','9','e','c'
    };
    HAL_UART_Transmit(&huart1, connect_packet, sizeof(connect_packet), 5000);
    vTaskDelay(300);

    /* 解析 CONNACK */
    uint8_t connack[4];
    int ret = pal_tcp_recv_raw(0, connack, 4, 3000);

    if (ret < 1)
    {
        /* 兜底：CONNACK 没收到，清缓冲后连发 30 次 CONNECT 报文，
           巴法云 TCP 透传模式下服务器收到连接请求就会建立会话 */
        RingBuf_Clear();
        for (int i = 0; i < 30; i++)
        {
            HAL_UART_Transmit(&huart1, connect_packet, sizeof(connect_packet), 5000);
            vTaskDelay(200);
        }

        /* 订阅主题 */
        uint8_t sub_packet[] = {
            0x82, 0x0C,
            0x00, 0x01,
            0x00, 0x07,
            's','t','m','3','2','f','4',
            0x01
        };
        HAL_UART_Transmit(&huart1, sub_packet, sizeof(sub_packet), 5000);
        vTaskDelay(300);

        g_mqtt_connected = 1;
        connect = 1;             /* 标记刚重连成功：心跳失败先不立即断网 */
        Log_Write(LOG_INFO, "reconnect success");
        return;
    }
}

/**
 * @brief MQTT 心跳包（AT 任务每 10 秒调用一次，防服务器踢下线）
 * PINGREQ 报文 = 0xC0 0x00
 */
void MQTT_SendPing(void)
{
    uint8_t ping[] = {0xC0, 0x00};
    HAL_UART_Transmit(&huart1, ping, 2, 2000);
}

/**
 * @brief 透传模式下从 ESP8266 接收原始 TCP 数据（读环形缓冲）
 * @param sock  未使用（保留参数）
 * @param buf   接收缓冲区指针
 * @param len   期望接收的字节数
 * @param timeout_ms 超时时间（毫秒）
 * @return 实际接收到的字节数，超时返回 -1
 */
int pal_tcp_recv_raw(int sock, uint8_t *buf, int len, int timeout_ms)
{
    (void)sock;
    uint32_t start = HAL_GetTick();
    int received = 0;
    while (received < len)
    {
        if (HAL_GetTick() - start > timeout_ms)
            return -1;

        uint8_t ch;
        while (RingBuf_Read(&ch) && received < len)
            buf[received++] = ch;

        vTaskDelay(1);           /* 避免 CPU 空转 */
    }
    return received;
}
