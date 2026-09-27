/**
  ******************************************************************************
  * @file    textlayout.h
  * @brief   UTF-8 文本排版工具（分类：排版模块）
  *
  *          只做「测量」和「折行」，不碰显存、不碰课表 —— 所以能单独测试，
  *          也能被别的界面复用。绘制通过回调交给调用方（见 TL_ForeachChar）。
  *
  *          支持 ASCII(8x16) 与中文(16x16) 混排：宽度不同，必须逐字解码累加，
  *          不能按「字符数」算（这是最容易出错的地方）。
  ******************************************************************************
  */
#ifndef __TEXTLAYOUT_H
#define __TEXTLAYOUT_H

#include <stdint.h>

/* 一行最多几个字符的「折行结果」缓冲上限 */
#define TL_MAX_LINES   4

/* 折行结果：每行在源字符串中的字节范围 + 该行像素宽度 */
typedef struct {
    int off[TL_MAX_LINES];    /* 每行起始字节偏移 */
    int len[TL_MAX_LINES];    /* 每行字节长度（不含结尾） */
    int px [TL_MAX_LINES];    /* 每行像素宽度（用于居中） */
    int count;                /* 实际行数 */
} TL_Lines;

/* ── 字符测量 ───────────────────────────────────────────────────────────────*/
/* 该字符的绘制宽度（像素）与前进步长（含 1px 字距）。
 * 返回 0 表示非法字节（调用方应跳过 *nbytes 字节）。*/
int TL_CharWidth(const uint8_t *p, int *adv, int *nbytes);

/* ── 折行 ───────────────────────────────────────────────────────────────────*/
/* 把 text[0..len) 按每行不超过 max_w 像素折行，最多 max_lines 行（超出丢弃）。
 * 结果写入 *out（调用方栈上分配即可）。
 * ★ 保证不溢出：任何一行宽度都 <= max_w。
 * ★ 数字串不断开：「主楼302」宁可整体换行，也不拆成「主楼30 / 2」。*/
void TL_Wrap(const char *text, int len, int max_w, int max_lines, TL_Lines *out);

/* ── 绘制 ───────────────────────────────────────────────────────────────────*/
/* 绘制回调：code=码点, x/y=像素位置, ctx=调用方上下文 */
typedef void (*TL_DrawFn)(uint16_t code, int x, int y, void *ctx);

/* 把 text 的第 line 行，以 cx 为水平中心、y 为基线，逐字回调 draw。*/
void TL_DrawLine(const char *text, const TL_Lines *lines, int line,
                 int cx, int y, TL_DrawFn draw, void *ctx);

#endif /* __TEXTLAYOUT_H */
