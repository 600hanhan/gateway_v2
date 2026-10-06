# 工业边缘网关（gateway_v2）

基于 STM32F411（Cortex-M4）的工业边缘网关，实现传感器数据采集、WiFi 联网上报、触摸屏实时显示与本地存储的完整数据链路。

## 功能特性

- **Modbus RTU 传感器采集**：通过 RS485 总线以 Modbus RTU 主站方式轮询温湿度、光照传感器，带 CRC16 校验
- **WiFi 联网上传**：ESP8266 通过 AT 指令接入网络，经 MQTT 协议连接巴法云服务器，定时上报传感器数据
- **LVGL 图形界面**：2.8 寸 TFT 触摸屏实时显示温度/湿度/光照，支持触摸交互
- **FreeRTOS 实时系统**：任务化架构（网络、采集、显示、心跳），消息队列实现任务间通信
- **SPI Flash 存储**：W25Q64 保存参数配置与离线数据缓存，断电不丢失
- **独立看门狗**：系统异常自动复位，保障长时间稳定运行

## 硬件平台

| 部件 | 型号 |
| --- | --- |
| 主控 | STM32F411CEU6 |
| 显示 | 2.8 寸 TFT LCD 触摸屏 |
| 通信 | ESP8266 WiFi 模块 |
| 存储 | W25Q64 SPI Flash |
| 采集 | RS485 Modbus 温湿度传感器 |

## 软件结构

| 目录 | 说明 |
| --- | --- |
| Core | 主程序、HAL 初始化、LCD/触摸/RTC/看门狗等板级驱动 |
| ESP8266 | WiFi AT 指令解析与 MQTT 协议实现 |
| SPI_Flash | W25Q64 底层驱动与数据缓存管理 |
| Lvgl | LVGL 图形库及自定义人机界面 |
| Middlewares/FreeRTOS | FreeRTOS 实时操作系统内核 |
| MDK-ARM | Keil MDK 工程文件 |

## 开发环境

- 集成开发环境：Keil MDK-ARM
- 芯片支持包：STM32F4xx DFP
- 代码生成：STM32CubeMX（HAL 库）
- 实时系统：FreeRTOS
- 图形库：LVGL

## 编译说明

使用 Keil MDK 打开 `MDK-ARM/gateway_v2.uvprojx`，编译下载即可。
