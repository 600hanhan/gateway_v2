/* ============================================================
 * modbus.c — Modbus RTU 主站采集
 * Modbus RTU 主站采集（RS485 总线）
 * 功能：CRC16 校验 + 0x03 读寄存器 + 多从站轮询 + 队列生产
 * ============================================================ */
#include "modbus.h"
#include "stm32f4xx.h"
#include "usart.h"
#include "stdio.h"
#include "string.h"
#include "FreeRTOS.h"
#include "task.h"
#include "Taskhandel.h"

extern QueueHandle_t data_queue;      /* 传感器数据队列（定义在 Taskhandel.c） */
extern QueueHandle_t lvgl_data_queue;  /* 显示数据队列（Taskhandel.c 定义）UI 屏幕实时刷新用 */
extern UART_HandleTypeDef huart2;     /* USART2：9600，PA2/PA3，接 RS485 总线 */

/* ============================================================
 * CRC16 查表法用到的两张表（由多项式 0x8005 预计算生成）
 * 高字节表 256 项 + 低字节表 256 项，直接查表免去逐位计算
 * ============================================================ */
static const uint8_t auchCRCHi[] = {
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40
};

static const uint8_t auchCRCLo[] = {
    0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06,
    0x07, 0xC7, 0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD,
    0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
    0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A,
    0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC, 0x14, 0xD4,
    0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
    0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3,
    0xF2, 0x32, 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4,
    0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
    0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29,
    0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF, 0x2D, 0xED,
    0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
    0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60,
    0x61, 0xA1, 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67,
    0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
    0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68,
    0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA, 0xBE, 0x7E,
    0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
    0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71,
    0x70, 0xB0, 0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92,
    0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
    0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B,
    0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89, 0x4B, 0x8B,
    0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
    0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42,
    0x43, 0x83, 0x41, 0x81, 0x80, 0x40
};

/* ============================================================
 * CRC16 Modbus 校验（面试高频考点！）
 * 多项式 0x8005；反向（右移）计算时用 0xA001；初值 0xFFFF；
 * 结果低字节先发（小端）。
 * flag=1 -> 按位循环法（容易理解，适合讲解）
 * flag=0 -> 查表法（速度快，适合量产）
 * ============================================================ */
uint16_t CRC_16_MODBUS(uint8_t *data, uint8_t len, uint8_t flag)
{
    if(flag)
    {
        /* -------- 按位循环法 -------- */
        uint16_t crc = 0xffff;      /* 初值 0xFFFF */
        while(len--)
        {
            crc ^= *data++;         /* 与数据字节异或 */
            for(int i = 0; i < 8; i++)  /* 逐位处理 8 个 bit */
            {
                if(crc & 1)         /* 最低位是 1：右移后异或 0xA001 */
                {
                    crc = (crc >> 1) ^ 0xA001;
                }
                else                /* 最低位是 0：直接右移 */
                {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }
    else
    {
        /* -------- 查表法（Modbus 标准 CRC 表） -------- */
        uint8_t CRCL = 0xff;        /* 低字节初值 */
        uint8_t CRCH = 0xff;        /* 高字节初值 */
        uint8_t index = 0;
        while(len--)
        {
            index = CRCL ^ *data++;         /* 低字节与数据异或得到查表下标 */
            CRCL = CRCH ^ auchCRCHi[index]; /* 新低字节 = 旧高字节 ^ 高表 */
            CRCH = auchCRCLo[index];        /* 新高字节 = 低表 */
        }
        return (CRCH << 8) | CRCL;  /* 拼成 16 位结果 */
    }
}

/* ============================================================
 * 多从站配置表（结构化配置，加设备只需加一行）
 *  {从机地址, 功能码, 起始寄存器, 寄存器个数, 错误计数, 数据1, 数据2}
 *  0x01 = 温湿度传感器；0x02 = 光照传感器
 * ============================================================ */
ModDev_t dev_list[] = {
    {0x01, 0x03, 0x0000, 2, 0, 0, 0}    /* 1# 温湿度：手册 0x00=温度 0x01=湿度，读 0x0000 起 2 个 */
    /* 光照传感器暂不接：接上并改成地址 0x02 后，取消下面这行的注释即可 */
    /* ,{0x02, 0x03, 0x0002, 2, 0, 0, 0} */
};
uint8_t dev_cnt = sizeof(dev_list) / sizeof(ModDev_t);   /* 自动算从站个数 = 2 */

/* ============================================================
 * 通用 Modbus 0x03 读寄存器（单从站单次事务）
 * 流程：组问询帧 -> 切发送方向 -> 发 8 字节 -> 切接收方向
 *      -> 轮询收帧（双重超时）-> 校验地址/长度/CRC -> 数据转存
 * @retval 1 成功，0 失败
 * ============================================================ */
uint8_t Modbus_Read_Dev(uint8_t addr, uint8_t func, uint16_t reg, uint16_t regcnt, uint16_t *buf)
{
    uint8_t tx[8];     /* 问询帧：地址+功能码+寄存器高+低+数量高+低+CRC低+CRC高 */
    uint8_t rx[32];    /* 应答缓存 */
    uint8_t count = 0; /* 实际收到字节数 */
    uint16_t crc = 0;

    /* ---- 1. 组问询帧（8 字节） ---- */
    tx[0] = addr;                     /* 从机地址 */
    tx[1] = func;                     /* 功能码 0x03 */
    tx[2] = (reg >> 8) & 0xFF;        /* 起始寄存器高字节 */
    tx[3] = reg & 0xFF;               /* 起始寄存器低字节 */
    tx[4] = (regcnt >> 8) & 0xFF;     /* 寄存器数量高字节 */
    tx[5] = regcnt & 0xFF;            /* 寄存器数量低字节 */
    crc = CRC_16_MODBUS(tx, 6, 0);    /* 前 6 字节算 CRC */
    tx[6] = crc & 0xFF;               /* CRC 低字节（先发） */
    tx[7] = (crc >> 8) & 0xFF;        /* CRC 高字节（后发） */

    memset(rx, 0, sizeof(rx));
    /* 关键：清空 UART 接收标志，清除上一轮残留数据 */
    __HAL_UART_CLEAR_FLAG(&huart2, UART_FLAG_RXNE | UART_FLAG_ORE);

    /* ---- 2. 发送（RS485 半双工：先拉高 PB0 进发送模式） ---- */
    RS485_TX;                          /* PB0 = 1，进入发送 */
    HAL_StatusTypeDef st = HAL_UART_Transmit(&huart2, tx, 8, 1000);
    /* 实测：st=0 表示发送成功；SR 看 RXNE(bit5)/ORE(bit3) 有没有收到自己发的字节 */
    printf("[MB] TX ret=%d SR=%08X\r\n", st, (uint32_t)huart2.Instance->SR);
    vTaskDelay(1);                     /* 等最后一个字节发完 */
    RS485_RX;                          /* PB0 = 0，切回接收 */
    /* ---- 调试打印①：确认发出去的帧（排障用） ----
       TX 8B 就是问询帧：01 03 00 00 00 02 C4 0B（符合 Modbus 协议标准帧） */
    printf("[MB] TX %dB: ", 8);
    for(uint8_t k = 0; k < 8; k++) printf("%02X ", tx[k]);
    printf("\r\n");

    /* ---- 3. 轮询接收（不依赖中断，读状态寄存器） ---- */
    uint32_t start = HAL_GetTick();
    uint32_t upd   = HAL_GetTick();
    count = 0;
    while(1)
    {
        /* 关键修复（排障）：RXNE 或 ORE 置位都要读 DR！*/
        /* F411 无 FIFO，溢出时数据挤在 DR；读 DR 是标准清错误流程（同时清 RXNE/ORE） */
        if(__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE) || __HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE))  /* 有数据 */
        {
            uint8_t ch = huart2.Instance->DR & 0xFF;      /* 读 DR 清标志 */
            if(count < sizeof(rx)) rx[count++] = ch;
            upd = HAL_GetTick();      /* 记录最后收到字节的时间 */
        }
        /* 帧间隔超时：收到过数据且 50ms 没新字节 = 一帧结束
           （Modbus 标准帧间隔是 3.5 字符 ≈4ms，50ms 足够宽松） */
        if(count > 0 && (HAL_GetTick() - upd) > 50) break;
        /* 绝对超时：800ms 没收到完整帧就放弃，绝不卡死 */
        if(HAL_GetTick() - start > 800) break;
        vTaskDelay(1);                /* 让出 CPU 给其他任务 */
    }


    /* ---- 调试打印②：确认收到多少字节、内容是什么 ----
       RX 0B = 传感器根本没应答（查硬件：供电/AB线/方向/共地）
       RX 9B = 收到应答了（看内容对不对） */
    printf("[MB] RX %dB: ", count);
    for(uint8_t k = 0; k < count; k++) printf("%02X ", rx[k]);
    printf("\r\n");

    /* ---- 4. 校验：长度 + 地址 + 功能码 + CRC ---- */
    uint8_t data_len = rx[2];                    /* 数据字节数（应答第 3 字节） */
    uint16_t crc_calc = CRC_16_MODBUS(rx, count - 2, 1);  /* 对收到的数据算 CRC */
    if(count == (3 + data_len + 2)               /* 帧长正确 */
        && rx[0] == addr && rx[1] == func        /* 地址/功能码正确 */
        && rx[count-2] == (crc_calc & 0xFF)      /* CRC 低字节匹配 */
        && rx[count-1] == ((crc_calc >> 8) & 0xFF)) /* CRC 高字节匹配 */
    {
        /* 数据转存：1 个寄存器 = 2 字节，高字节在前（大端） */
        for(uint8_t i = 0; i < regcnt; i++)
        {
            buf[i] = (rx[3 + i*2] << 8) | rx[4 + i*2];
        }
        return 1;
    }
    printf("[MB] 校验失败: cnt=%d len=%d crc=%04X\r\n", count, 3+data_len+2, crc_calc);
    return 0;
}

/* ============================================================
 * 整表轮询：遍历所有从站读一遍，结果写回 dev_list[i].val1/val2
 * 每个从站之间间隔 30ms，防总线干扰
 * ============================================================ */
void Modbus_Poll_AllDev(void)
{
    uint16_t tmp_buf[8];
    for(uint8_t i = 0; i < dev_cnt; i++)
    {
        ModDev_t *p = &dev_list[i];
        memset(tmp_buf, 0, sizeof(tmp_buf));   /* 每次读前清空缓存 */

        if(Modbus_Read_Dev(p->dev_addr, p->func, p->reg_start, p->reg_num, tmp_buf) == 1)
        {
            /* 根据从机地址解析原始寄存器值 */
            switch(p->dev_addr)
            {
                case 0x01:  /* 温湿度：0x00=温度 0x01=湿度，均含 1 位小数 ÷10 */
                    if(tmp_buf[0] & 0x8000)          /* 负温度：最高位为 1（补码），先取绝对值再加负号 */
                    {
                        p->val1 = -((tmp_buf[0] & 0x7FFF) / 10.0f);
                    }
                    else                             /* 正温度 */
                    {
                        p->val1 = tmp_buf[0] / 10.0f;
                    }
                    p->val2 = tmp_buf[1] / 10.0f;    /* 湿度 %RH */
                    break;
                case 0x02:  /* 光照：32 位合成后 /1000 = lux */
                    p->val1 = ((uint32_t)tmp_buf[0] << 16 | tmp_buf[1]) / 1000;
                    p->val2 = 0;
                    break;
                default: break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));   /* 单从机间隔，防总线冲突 */
    }
}

/* ============================================================
 * 把采集结果打包成 Data_t 写入 data_queue（覆盖式，容量 1）
 * MQTT 任务阻塞收这条队列
 * ============================================================ */
void Modbus_Upload_Json(void)
{
    Data_t uidata;
    uidata.temp  = dev_list[0].val1;   /* 温度 */
    uidata.shi   = dev_list[0].val2;   /* 湿度 */
    uidata.light = (dev_cnt > 1) ? dev_list[1].val1 : 0;   /* 光照：没接时为 0，防越界 */
    xQueueOverwrite(data_queue, &uidata);  /* 覆盖写：永远保留最新 */
    xQueueOverwrite(lvgl_data_queue, &uidata);  /* 同一份数据再喂 UI 队列 -> 屏幕温度/湿度/光照实时刷新 */
}
