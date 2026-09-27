/**
  ******************************************************************************
  * @file    log.c
  * @brief   串口调试打点（USART1, 115200-8-N-1）
  *
  *          阻塞式发送，每字节等 TXE —— 简单可靠，不占中断，够调试用。
  ******************************************************************************
  */
#include "log.h"
#include "main.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* CubeMX 配 USART1 后生成。用 extern 弱引用，避免未配串口时链接失败。*/
extern UART_HandleTypeDef huart1;

static uint8_t  log_ready = 0;
static char     log_buf[192];

void LOG_Init(void)
{
    log_ready = 1;
}

/* 底层：一个字节一个字节发（HAL_UART_Transmit 阻塞发送） */
static void log_put(const char *s, uint16_t n)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, n, HAL_MAX_DELAY);
}

void LOGF(const char *fmt, ...)
{
    if (!log_ready) return;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(log_buf, sizeof(log_buf), fmt, ap);
    va_end(ap);

    if (n <= 0) return;
    if (n > (int)sizeof(log_buf) - 1) n = sizeof(log_buf) - 1;

    log_put(log_buf, (uint16_t)n);
}
