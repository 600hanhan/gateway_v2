/* ============================================================
 * led.c — PC13 状态指示灯驱动（新增）
 * 指示灯语义（红=正常 / 黄=离线 / 绿=补传），
 * 改用板载 PC13 单灯表达：正常常亮 / 离线熄灭 / 补传闪烁。
 * 这样不需要外接三色 LED，一颗板载灯就能看出 MQTT 状态。
 * ============================================================ */
#include "led.h"
#include "stm32f4xx.h"

/* ---- PC13 极性说明 ----
 * 大多数开发板板载 LED 是低电平点亮（LED 阳极接 3V3、阴极接 PC13）。
 * 如果烧录后发现灯亮灭反了，把 LED_ON/LED_OFF 里的 SET/RESET 对调即可。 */
#define LED_PORT    GPIOC
#define LED_PIN     GPIO_PIN_13
#define LED_ON()    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET)  /* 低电平点亮 */
#define LED_OFF()   HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET)    /* 高电平熄灭 */
#define LED_TOGGLE() HAL_GPIO_TogglePin(LED_PORT, LED_PIN)                /* 翻转 */

static LedState_t s_state = LED_NORMAL;  /* 当前状态（默认正常） */
static uint32_t s_blink_tick = 0;        /* 闪烁计时基准（补传用） */

/* ============================================================
 * PC13 初始化：代码里直接配置 GPIO（不依赖 CubeMX 生成的 gpio.c）
 * 手动做法：CubeMX 里点 PC13 -> 选 GPIO_Output（推挽、低速），
 * 重新生成代码；或直接保留本函数用代码配置，二者等效。
 * ============================================================ */
void Led_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();                 /* 1. 打开 GPIOC 外设时钟 */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = LED_PIN;              /* 2. 选中 PC13 */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;  /* 3. 推挽输出（能灌/拉电流驱动 LED） */
    GPIO_InitStruct.Pull  = GPIO_NOPULL;          /* 4. 不上拉不下拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;  /* 5. 低速足够（LED 翻转不快） */
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
    LED_ON();                                     /* 上电默认：正常常亮 */
}

/* 设置状态：由 MQTT 任务按在线/离线/补传调用 */
void Led_SetState(LedState_t st)
{
    s_state = st;
}

/* ============================================================
 * 周期驱动（每个任务循环里调一次即可）：
 *   正常 -> 常亮；离线 -> 熄灭；补传 -> 500ms 翻转 = 闪烁
 * ============================================================ */
void Led_Process(void)
{
    switch(s_state)
    {
        case LED_NORMAL:   /* 在线：常亮 */
            LED_ON();
            break;
        case LED_OFFLINE:  /* 离线：熄灭 */
            LED_OFF();
            break;
        case LED_CATCHUP:  /* 补传：500ms 翻转一次 */
            if(HAL_GetTick() - s_blink_tick >= 500)
            {
                s_blink_tick = HAL_GetTick();
                LED_TOGGLE();
            }
            break;
        default:
            LED_ON();
            break;
    }
}
