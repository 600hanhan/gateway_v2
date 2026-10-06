#include "spi.h"
#include "touch.h"
#include "stdio.h"
#include "usart.h"
#include "string.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_flash.h"   /* SPI1 总线互斥锁（触摸与板载Flash共用SPI1） */
extern SPI_HandleTypeDef hspi1;  // 改用 SPI1
extern TickType_t idle_tick;
extern uint8_t lcd_bl_en;

 uint16_t pressure;
 

 
static uint8_t TP_ReadWrite(uint8_t data)
{
    uint8_t rx;
    HAL_SPI_TransmitReceive(&hspi1, &data, &rx, 1, 100);
    return rx;
}

static uint16_t TP_Read_AD(uint8_t cmd)
{
    uint16_t temp = 0;
	SPI1_LOCK();             /* 独占 SPI1 总线：防 Flash 读写打断触摸采样 */
	LCD_CS_SET;
    TP_CS_CLR;
    TP_ReadWrite(cmd);
    temp = TP_ReadWrite(0xFF) << 8;
    temp |= TP_ReadWrite(0xFF);
    TP_CS_SET;
    SPI1_UNLOCK();
    temp >>= 4;
    return temp;
}

uint16_t TP_ReadX(void)
{
    return TP_Read_AD(0xD0);
}

uint16_t TP_ReadY(void)
{
    return TP_Read_AD(0x90);
}

// 获取压力值（Z1、Z2）
static uint16_t TP_ReadPressure(void)
{
    uint16_t z1 = TP_Read_AD(0xB1);
    uint16_t z2 = TP_Read_AD(0xC1);
    if (z1 == 0) return 0;
    return (z2 * 100) / z1;   // 得压力值
}

void TP_Init(void)
{
    TP_CS_SET;
   HAL_Delay(10);  // ✅ 必须用这个
    TP_ReadX();  // 预热
}

 // 新增：检测触摸是否按下
static uint8_t TP_IsPressed(void)
{
    uint16_t z1 = TP_Read_AD(0xB1);
    uint16_t z2 = TP_Read_AD(0xC1);
    
    // 常见触摸阈值：Z1 < 1000 或 Z2 < 1000 即认为按下（可微调）
    if (z1 >50 && z2 < 3000)  // 阈值根据实际触摸调试
        return 1;
    else
        return 0;
}

uint8_t TP_Get_Calibrated(uint16_t *x, uint16_t *y)
{
    // ========== 1. 压力检测（先排除无触摸情况） ==========
    // 1. 先判断是否按下
    if (!TP_IsPressed())
        return 0;

		
		    //====触摸按下：亮屏+重置空闲计时====
    idle_tick = xTaskGetTickCount();
    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_8,GPIO_PIN_SET);
    lcd_bl_en = 1;
		
		
    // 2. 读取坐标
    uint16_t raw_x = TP_ReadX();
    uint16_t raw_y = TP_ReadY();
    printf("raw_x=%d raw_y=%d\r\n", raw_x, raw_y);   /* temp debug */

    // ========== 3. 范围滤波（此段已注释关闭） ==========
    // if (raw_x < 200 || raw_x > 4095 || raw_y < 200 || raw_y > 4095)
    //     return 0;

    // ========== 4. 坐标映射（使用已验证正确的换算公式） ==========
    int16_t lcd_x, lcd_y;

    // 确认是 2.8寸屏参数
    #define TOUCH_SWAP_XY    1
    #define TOUCH_MIRROR_X   1
    #define TOUCH_MIRROR_Y   1




    #define TX_MIN   170
    #define TX_MAX   1900
    #define TY_MIN   190
    #define TY_MAX   1900

#if TOUCH_SWAP_XY == 0
    lcd_x = (raw_x - TX_MIN) * (LCD_W - 1) / (TX_MAX - TX_MIN);
    lcd_y = (raw_y - TY_MIN) * (LCD_H - 1) / (TY_MAX - TY_MIN);
#else
    lcd_x = (raw_y - TY_MIN) * (LCD_W - 1) / (TY_MAX - TY_MIN);
    lcd_y = (raw_x - TX_MIN) * (LCD_H - 1) / (TX_MAX - TX_MIN);
#endif

    if (TOUCH_MIRROR_X) lcd_x = (LCD_W - 1) - lcd_x;
    if (TOUCH_MIRROR_Y) lcd_y = (LCD_H - 1) - lcd_y;

    if (lcd_x < 0) lcd_x = 0;
    if (lcd_x >= LCD_W) lcd_x = LCD_W - 1;
    if (lcd_y < 0) lcd_y = 0;
    if (lcd_y >= LCD_H) lcd_y = LCD_H - 1;

    *x = (uint16_t)lcd_x;
    *y = (uint16_t)lcd_y;
    return 1;
}
