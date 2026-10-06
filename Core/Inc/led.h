#ifndef LED_H
#define LED_H
#include "stm32f4xx.h"

/* ============================================================
 * PC13 状态指示灯（新增）
 * 板载三色外接 LED（PA15 红 / PB3 黄 / PB4 绿），
 * 用板载 PC13 一个灯表达 MQTT 三种状态：
 *   正常(在线) -> 常亮      离线 -> 熄灭      补传 -> 闪烁
 * ============================================================ */
typedef enum
{
    LED_NORMAL = 0,   /* MQTT 正常在线：PC13 常亮 */
    LED_OFFLINE,      /* MQTT 离线：PC13 熄灭 */
    LED_CATCHUP       /* 离线期间补传数据：PC13 闪烁 */
} LedState_t;

void Led_Init(void);          /* 配置 PC13 为推挽输出（代码配置，免改 CubeMX） */
void Led_SetState(LedState_t st);  /* 设置当前状态 */
void Led_Process(void);       /* 周期调用（放任务循环里）：按状态驱动 PC13 */

#endif
