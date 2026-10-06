#ifndef PAL_H
#define PAL_H

#include "main.h"
#include <string.h>
#include "usart.h"
#include "stm32f4xx.h"
#include "stdio.h"

/* ============================================================
 * 用户专属网络配置（烧录前必须改成你自己的！）
 * ============================================================ */
#define WIFI_SSID    "hihihi"                       /* 你家 WiFi 名称 */
#define WIFI_PASS    "08080809"               /* 你家 WiFi 密码 */
#define BEMFA_BROKER "bemfa.com"                 /* 巴法云服务器地址（免费物联网平台） */
#define BEMFA_PORT   "9501"                      /* 巴法云 TCP 透传端口（字符串，直接拼进 AT 指令） */
#define BEMFA_UID    "0819071d7cf0161f696bb04bf1b639ec"  /* 巴法云私钥（注册后控制台看） */
#define TOPIC_PUB    "stm32f4"                     /* MQTT 主题（巴法云控制台创建） */

/* ============================================================
 * 网络协议栈接口
 * ============================================================ */
void MQTT_Init(void);          /* 首次连接：WiFi -> TCP透传 -> MQTT CONNECT/SUBSCRIBE */
void RE_MQTT_Init(void);       /* 断线重连 */
void MQTT_SendPing(void);      /* 发 MQTT 心跳包 PINGREQ (0xC0 0x00) */
int  pal_tcp_recv_raw(int sock, uint8_t *buf, int len, int timeout_ms);  /* 透传模式下收原始TCP数据 */

#endif
