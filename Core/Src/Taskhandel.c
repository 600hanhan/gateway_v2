/* ============================================================
 * Taskhandel.c — 五个 RTOS 任务（占位版）
 * 任务名/优先级/栈配置
 * 本课任务只打印心跳验证调度，真实逻辑后续课程逐个填充：
 *    -> Modbus 轮询 + data_queue 生产
 *    -> MQTT 消费队列 + 离线 Flash 缓存
 *    -> UITask 换成 LVGL 渲染
 *    -> AT 换成 ESP8266 真实心跳
 * ============================================================ */
#include "Taskhandel.h"
#include "task.h"
#include "main.h"
#include "iwdg.h"
#include "usart.h"
#include "stdio.h"
#include "string.h"   /* strlen：离线缓存 JSON 长度 */
#include "modbus.h"   /* Modbus 主站采集 */
#include "command.h"  /* ESP8266 环形缓冲 */
#include "pal.h"       /* MQTT_Init/心跳/重连 */
#include "led.h"      /* PC13 状态灯 */
#include "lcd.h"          /* ILI9341 LCD 驱动 */
#include "lvgl.h"         /* LVGL 图形库 */
#include "lv_port_disp.h" /* LVGL 显示端口适配 */
#include "page_main.h"    /* 主监控页面 */
#include "lv_port_indev.h" /* LVGL 触摸输入适配 */
#include "touch.h"
#include "flash.h"      /* SPI Flash 日志分区接口：Log_Write/Log_ReadOne */      /* XPT2046 触摸驱动：TP_Init/TP_Get_Calibrated */

/* 看门狗句柄：iwdg.h 里已 extern 声明，直接使用 */
extern IWDG_HandleTypeDef hiwdg;

/* ===== 任务间同步对象【定义】 =====
   ★ 这就是刚才 4 个链接报错的修复：
   头文件里的 extern 只是"声明"（告诉编译器：有这个变量，别处定义），
   这里才是"定义"（真正分配存储空间）。声明了却没人定义 -> L6218E。
   flash_mutex 定义于 Taskhandel.c，创建于 main.c。 */
QueueHandle_t     data_queue;        /* 传感器数据队列：SensorTask 生产，MQTTTask 消费 */
QueueHandle_t     lvgl_data_queue;   /* 显示数据队列：MQTTTask 生产，UITask 消费 */
SemaphoreHandle_t uart1_mutex;       /* USART1 互斥量：保护 ESP8266 串口 */
SemaphoreHandle_t flash_mutex;       /* SPI Flash 互斥量：保护 Flash 读写 */
/* ===== LVGL / UI 全局状态（对 Taskhandel.c 第32-46行） ===== */
lv_obj_t *main_scr;                    /* 主页面句柄：page_main_create() 返回 */
volatile uint8_t g_mqtt_connected = 0; /* MQTT 连接状态：0=离线 1=在线（pal.c 连上置 1） */
volatile uint8_t connect = 0;            /* 0=正常 1=刚重连成功（，防心跳误判断网） */
TickType_t idle_tick;                  /* 空闲计时：用于 10s 无操作自动息屏 */
uint8_t lcd_bl_en = 1;                 /* 背光使能：1=亮屏 0=熄屏（PA8） */
uint32_t log_write_offset = 0;         /* 日志写指针：SPI Flash 日志（接入） */

/* ------------------------------------------------------------
 * UI 任务：栈 1024word(4KB) 优先级 AboveNormal
 * 会换成：LVGL 初始化 + 界面渲染 + 消费 lvgl_data_queue
 * ---------------------------------------------------------- */
void UI(void *argument)
{
    /* ===== LVGL 初始化（对 Taskhandel.c 的 UI() 函数） ===== */
    LCD_Init();
    Log_Write(LOG_INFO, "System boot OK");   /* 开机事件写一条日志到 W25Q */                      /* 1. 初始化 ILI9341 液晶屏 */
    lv_init();                       /* 2. 初始化 LVGL 图形库 */
    lv_port_disp_init();             /* 3. 注册显示驱动（显示缓冲区 + flush 回调） */
    TP_Init();                   /* 3.5 初始化触摸芯片 XPT2046（复用 SPI1） */
    lv_port_indev_init();        /* 3.6 注册触摸输入设备（LVGL 就能收到点击） */

    main_scr = page_main_create();   /* 4. 创建主监控页面（三卡片+状态栏） */
    lv_scr_load(main_scr);           /* 5. 把主页面加载到屏幕显示 */

    Data_t disp_data;                /* 接收传感器数据的临时变量 */
    uint32_t start_tick = xTaskGetTickCount();
    uint8_t  last_status = 0xFF;     /* 上一次 MQTT 状态（初始化为不可能值，强制刷新一次） */
    uint32_t last_uptime = 0xFFFFFFFF;

    while (1)
    {
        /* 6. 空闲 10s 无操作 -> 息屏（PA8 背光灭） */
        if (lcd_bl_en == 1 && (xTaskGetTickCount() - idle_tick >= pdMS_TO_TICKS(10000)))
        {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
            lcd_bl_en = 0;
        }

        /* 7. 收到传感器数据 -> 更新界面（数据变化超过阈值时，update_data 内部会自动亮屏） */
        if (xQueueReceive(lvgl_data_queue, &disp_data, 0) == pdPASS)
        {
            page_main_update_data(disp_data.temp, disp_data.shi, disp_data.light);
        }

        /* 8. MQTT 连接状态变化 -> 更新底部状态栏（在线/离线） */
        if (g_mqtt_connected != last_status)
        {
            last_status = g_mqtt_connected;
            page_main_update_status(g_mqtt_connected);
        }

        /* 9. 每秒更新一次运行时间 */
        uint32_t uptime = (xTaskGetTickCount() - start_tick) / configTICK_RATE_HZ;
        if (uptime != last_uptime)
        {
            last_uptime = uptime;
            page_main_update_uptime(uptime);
            page_main_update_rtc();   /* RTC 时间每秒同步刷新（主界面底部中间） */
        }

        lv_timer_handler();          /* 10. LVGL 定时器处理（把脏区域刷新到屏幕） */
        vTaskDelay(5);               /* 5ms 让出 CPU */
    }
}
void Modbus(void *argument)
{
    for(;;)
    {
        Modbus_Poll_AllDev();   /* 遍历所有从站读一遍，结果写回 dev_list */
        Modbus_Upload_Json();   /* 打包成 Data_t 写入 data_queue（覆盖式） */
        /* 采集事件写日志。内容用 ASCII（中文字库只有58个页面用字，缺字会显示方框） */
        char log_buf[64];
        snprintf(log_buf, sizeof(log_buf), "TH ok T%.1f H%.1f", dev_list[0].val1, dev_list[0].val2);
        Log_Write(LOG_INFO, log_buf);
        printf("[Sensor] 温度:%.1f 湿度:%.1f%% 光照:%.0f\r\n",
               dev_list[0].val1, dev_list[0].val2,
               (dev_cnt > 1) ? dev_list[1].val1 : 0);  /* 光照没接时为0，防 dev_list 越界 */
        vTaskDelay(800);        /* 800ms 一轮（对：Modbus_Poll_AllDev + Upload_Json + 800ms） */
    }
}

/* ------------------------------------------------------------
 * 网络 AT 任务（NetworkTask）：优先级 Normal
 * 会换成：ESP8266 AT 指令 + 10s 心跳 + 断线重连
 * ---------------------------------------------------------- */
void AT(void *argument)
{
    /* =====  Taskhandel.c 的 AT() 任务 =====
       MQTT_Init() 一次性完成：WiFi连接 -> TCP透传 -> MQTT CONNECT/SUBSCRIBE
       之后每 10 秒发一次 PINGREQ 心跳，防服务器踢下线；
       连续 3 次心跳无 PINGRESP -> 判离线 -> RE_MQTT_Init() 重连。 */
    MQTT_Init();                     /* 连接成功后 g_mqtt_connected 为 1 否则为 0（pal.c） */

    uint8_t heart_fail = 0;          /* 心跳连续失败次数 */
    for (;;)
    {
        vTaskDelay(10000);           /* 每 10 秒检测一次心跳 */
        xSemaphoreTake(uart1_mutex, portMAX_DELAY);   /* 阻塞获取互斥锁（防 MQTT 任务同时用串口） */
        RingBuf_Clear();             /* 关键点：先清空缓冲，否则有脏数据影响判断 */

        if (g_mqtt_connected)
        {
            MQTT_SendPing();         /* 发 PINGREQ (0xC0 0x00) */

            uint8_t pong[2];
            int ret = pal_tcp_recv_raw(0, pong, 2, pdMS_TO_TICKS(1000));  /* 等 PINGRESP，1秒超时 */

            if (ret >= 2 && pong[0] == 0xD0 && pong[1] == 0x00)
            {
                heart_fail = 0;      /* 心跳正常 */
                connect = 0;
            }
            else                     /* 心跳超时或数据错误 */
            {
                if (connect == 0)    /* 刚重连成功时不立即计数，给新连接机会 */
                {
                    heart_fail++;
                    if (heart_fail >= 3)   /* 连续 3 次失败，约 30 秒 */
                    {
                        g_mqtt_connected = 0;          /* 断网（MQTT 任务看到后走离线分支） */
                        Log_Write(LOG_WARN, "MQTT disconnect");
                        heart_fail = 0;
                    }
                }
            }
        }
        else
        {
            RE_MQTT_Init();          /* 断网重连 */
        }
        xSemaphoreGive(uart1_mutex); /* 释放锁 */
    }
}

/* ------------------------------------------------------------
 * MQTT 任务（MQTTTask）：优先级 Normal
 *  Taskhandel.c 的 MQTT() 任务接入断网缓存补传
 *   在线：先补传 SPI Flash 缓存（每轮最多 5 条）再发实时 -> PC13 常亮
 *   离线：采集数据写 SPI Flash 缓存分区（重连后自动补传） -> PC13 灭
 *   补传：PC13 闪烁
 * ---------------------------------------------------------- */
void MQTT(void *argument)
{
    vTaskDelay(5000);                    /* 开等 5 秒：等 Flash/UI 初始化就绪 */
    Data_t data_q;
    for(;;)
    {
        /* 阻塞收 Modbus 采集数据（Modbus 任务 800ms 一轮写入 data_queue） */
        xQueueReceive(data_queue, &data_q, portMAX_DELAY);

        if (g_mqtt_connected)            /* ===== 在线：先补传历史缓存，再发实时 ===== */
        {
            char cached[512];
            uint16_t len;
            int count = 0;
            xSemaphoreTake(uart1_mutex, portMAX_DELAY);   /* 独占 ESP8266 串口 */

            /* 补传循环：读一条 -> 发一条 -> 标记已发；读完即销毁，绝对不重复 */
            while (FlashCache_HasData() && count < 5)     /* 读指针落后写指针=有缓存 */
            {
                if (FlashCache_Read(cached, &len) == 0)   /* 从读指针位置读一条缓存 */
                {
                    Led_SetState(LED_CATCHUP);            /* 补传中：PC13 闪烁 */
                    Led_Process();
                    MQTT_PublishJson(cached);             /* 发布这条缓存数据 */
                    FlashCache_MarkSent();                /* 只改魔数标记已补传，不擦除 */
                    count++;
                    if (count > 4)
                        Log_Write(LOG_INFO, "BUCHUAN Finish");  /* 补满 5 条记一条日志 */
                    vTaskDelay(800);                      /* 每条间隔 800ms */
                }
                else
                {
                    my_clear();          /* 读指针追平写指针 -> 清空缓存 */
                    break;
                }
            }

            /* 发实时数据（单条 JSON 上报） */
            char json[128];
            sprintf(json, "{\"\xE6\xB8\xA9\xE5\xBA\xA6\":%.1f,\"\xE6\xB9\xBF\xE5\xBA\xA6\":%.1f,\"\xE5\x85\x89\xE7\x85\xA7\":%.0f}",   /* 字段名=温度/湿度/光照，UTF-8字节转义，巴法云显示中文 */
                    data_q.temp, data_q.shi, data_q.light);
            MQTT_PublishJson(json);

            Led_SetState(LED_NORMAL);                     /* 在线正常：PC13 常亮 */
            Led_Process();
            xSemaphoreGive(uart1_mutex);                  /* 释放串口锁 */
        }
        else                             /* ===== 离线：写 Flash 缓存，等重连补传 ===== */
        {
            char json[128];
            sprintf(json, "{\"\xE6\xB8\xA9\xE5\xBA\xA6\":%.1f,\"\xE6\xB9\xBF\xE5\xBA\xA6\":%.1f,\"\xE5\x85\x89\xE7\x85\xA7\":%.0f}",   /* 字段名=温度/湿度/光照，UTF-8字节转义，巴法云显示中文 */
                    data_q.temp, data_q.shi, data_q.light);
            Led_SetState(LED_OFFLINE);                    /* 离线：PC13 灭 */
            Led_Process();
            FlashCache_Write(json, strlen(json));         /* 写缓存分区，等重连后补传 */
            printf("[MQTT] 离线缓存 write\r\n");
        }
    }
}

/* ------------------------------------------------------------
 * 看门狗任务（Dog_task）：优先级最高 AboveNormal+2
 * 独立看门狗 IWDG 20s 溢出，本任务每 3s 喂一次狗。
 * 如果系统被某个任务卡死(死循环/死锁)，没人喂狗，
 * 20s 后硬件自动复位，把系统救活——这就是"自愈"。
 * ---------------------------------------------------------- */
void Dog_task(void *argument)
{
    for(;;)
    {
        HAL_IWDG_Refresh(&hiwdg);   /* 喂狗：重置 20s 倒计时 */
        printf("[Dog] 喂狗 OK\r\n");
        vTaskDelay(3000);           /* 3 秒喂一次 */
    }
}
