/**
  ******************************************************************************
  * @file    log.h
  * @brief   串口调试打点（USART1, 115200-8-N-1）
  *
  *          用途：屏不亮时定位固件跑到哪一步、各阶段耗时、引脚实际电平。
  *          用法：LOG("...") 或 LOGF("x=%d", x)，支持 printf 风格。
  *          依赖：CubeMX 配好 USART1（PA9=TX, PA10=RX），并在 main.c 调 LOG_Init()。
  ******************************************************************************
  */
#ifndef __LOG_H
#define __LOG_H

#include <stdint.h>

/* 初始化串口日志（在 MX_USART1_UART_Init() 之后调用） */
void LOG_Init(void);

/* printf 风格的打点 */
void LOGF(const char *fmt, ...);

/* 宏：LOG("text") 等价于 LOGF("text") */
#define LOG(...) LOGF(__VA_ARGS__)

#endif /* __LOG_H */
