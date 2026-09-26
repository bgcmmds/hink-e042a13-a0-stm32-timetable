/**
  ******************************************************************************
  * @file    epd.h
  * @brief   4.2" 墨水屏驱动（400x300 黑白）
  *
  *          控制器：SSD1619A（命令集与 SSD1680 系列兼容，见 epd.c 顶部说明）
  *          分辨率：400 x 300，1bpp 黑白
  *          接线：SPI1 (PA5 SCK / PA7 MOSI) + 4 个控制 GPIO
  *          坐标系：原点左上角，X 向右 0..399，Y 向下 0..299
  ******************************************************************************
  */
#ifndef __EPD_H
#define __EPD_H

#include <stdint.h>

/* 屏幕分辨率 ----------------------------------------------------------------*/
#define EPD_WIDTH   400
#define EPD_HEIGHT  300

/* 显存：1 bit/像素，1=白 0=黑（与 RGB565 相反，符合墨水屏习惯）---------------
 * 400 x 300 / 8 = 15000 字节，行对齐到 8bit（400 正好是 8 的整数倍，无 padding）*/
#define EPD_BUF_SIZE  (EPD_WIDTH / 8 * EPD_HEIGHT)   /* = 15000 */

/* 初始化 + 把整个显存清成白色（不会自动刷新到屏上，需再调 EPD_Display） */
void EPD_Init(void);

/* 显存操作（只改内存，不刷新）-----------------------------------------------*/
void EPD_Clear(uint8_t color);                       /* color: 0=黑 1=白 */
void EPD_DrawPixel(int16_t x, int16_t y, uint8_t color);
void EPD_DrawHLine(int16_t x, int16_t y, int16_t w, uint8_t color);
void EPD_DrawVLine(int16_t x, int16_t y, int16_t h, uint8_t color);
void EPD_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);            /* 空心 */
void EPD_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);            /* 实心 */

/* 文字绘制（依赖 font.c 的 ASCII 8x16 字库）---------------------------------*/
/* 画一个字符；返回字符宽度(8)，方便连续绘制 */
int16_t EPD_DrawChar(int16_t x, int16_t y, uint8_t ch, uint8_t color);

/* 画字符串；自动换行（超出 EPD_WIDTH 时回到 x0 并下移一行）。
 * 返回实际绘制完的下一行 y 坐标。color: 0=黑字 1=白字
 * 反白用法：先用 EPD_FillRect 填黑底，再用 color=1 画白字。 */
int16_t EPD_DrawString(int16_t x, int16_t y, const char *str, uint8_t color);

/* 把显存推到屏上（含 BUSY 等待，全刷约 2~4 秒） */
void EPD_Display(void);

/* 刷新后进深度睡眠省电（墨水屏断电保持画面，静态显示不耗电） */
void EPD_Sleep(void);

/* 直接取显存指针（课程表模块绘制时用得到） */
uint8_t* EPD_GetBuffer(void);

/* ★ 硬件自检用：依次显示 4 种测试画面，每张停 3 秒。
 *   用来验证 SPI 接线、极性、分辨率、显存对齐是否正确。 */
void EPD_TestPattern(void);

/* ★ 串口诊断（临时排查用，实现在 epd_test.c）：
 *   逐步打点报告引脚电平、BUSY 耗时，定位屏不亮的原因。 */
void EPD_Diag(void);

#endif /* __EPD_H */
