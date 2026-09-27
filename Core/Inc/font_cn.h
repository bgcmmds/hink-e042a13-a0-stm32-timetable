/**
  ******************************************************************************
  * @file    font_cn.h
  * @brief   中文字库接口（分类：字体模块）
  *
  *          与 font.c 的 ASCII 8x16 并存：ASCII 走 FONT_GetAscii()，
  *          中文走 FONT_GetChinese()。点阵格式完全一致（纵向取模、低位在上），
  *          只是汉字是 16x16 宽，所以每字 32 字节。
  *
  *          数据由 tools/font_export.py 从 TTF 生成，不要手写。
  ******************************************************************************
  */
#ifndef __FONT_CN_H
#define __FONT_CN_H

#include <stdint.h>

/* 汉字点阵尺寸（16x16，与 ASCII 同高，排版时基线能对齐） */
#define FONT_CN_W           16
#define FONT_CN_H           16
#define FONT_CN_BYTES       32                  /* (16/8)*16 */

/* 取汉字字模：code 是 Unicode 码点（UTF-8 解码后的值）。
 * 字库里没有这个字时返回 NULL，调用方需判空。 */
const uint8_t* FONT_GetChinese(uint16_t code);

#endif /* __FONT_CN_H */
