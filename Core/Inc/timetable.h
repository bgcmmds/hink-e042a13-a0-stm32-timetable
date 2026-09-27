/**
  ******************************************************************************
  * @file    timetable.h
  * @brief   课程表布局与绘制（分类：业务模块）
  *
  *          布局：5 天（周一~周五）× 5 大节
  *            上午第1节 / 上午第2节 / 下午第1节 / 下午第2节 / 晚上
  *
  *          尺寸分配（屏 400x300）：
  *            ┌──────┬────┬────┬────┬────┬────┐
  *            │ 节次 │ 一 │ 二 │ 三 │ 四 │ 五 │  ← 表头 20px
  *            ├──────┼────┼────┼────┼────┼────┤
  *            │ 上午1│    │    │    │    │    │  ← 每行 56px
  *            │ 上午2│    │    │    │    │    │
  *            │ 下午1│    │    │    │    │    │
  *            │ 下午2│    │    │    │    │    │
  *            │ 晚上 │    │    │    │    │    │
  *            └──────┴────┴────┴────┴────┴────┘
  *              56px   ← 每列 68px →
  ******************************************************************************
  */
#ifndef __TIMETABLE_H
#define __TIMETABLE_H

#include <stdint.h>
#include "epd.h"      /* 需要 EPD_WIDTH / EPD_HEIGHT 算格子尺寸 */

/* 网格参数 ----------------------------------------------------------------*/
#define TT_DAYS        5      /* 一周 5 天（周一~周五） */
#define TT_PERIODS     5      /* 每天 5 大节 */

#define TT_LEFT_W      52     /* 左侧「节次」列宽（像素）。中文节次名 3 字宽=50px，
                              * 留 1px 边距 → 52。若改回 ASCII 名可收窄。 */
#define TT_HEAD_H      20     /* 表头行高（像素） */

/* 每格宽高（自动算，改上面的参数会自动适配） */
#define TT_CELL_W      ((EPD_WIDTH - TT_LEFT_W) / TT_DAYS)    /* (400-40)/5 = 72 */
#define TT_CELL_H      ((EPD_HEIGHT - TT_HEAD_H) / TT_PERIODS) /* (300-20)/5 = 56 */

/* 单格课程内容长度上限（含结尾 '\0'） */
#define TT_COURSE_MAX  24
#define TT_PLACE_MAX   16     /* 上课地点字符串上限（含 '\0'） */

/* 单格最多显示行数（换行缓冲上限）。按字高 16px、格高 56px 取 3 行足够 */
#define TT_LINE_MAX    4

/* 气泡样式参数 --------------------------------------------------------------*/
#define TT_BUBBLE_BAR_W   4   /* 左侧色条宽度（px），气泡的视觉标识 */
#define TT_BUBBLE_GAP     2   /* 气泡与格子边界的内缩（px），留缝隙更易区分 */
#define TT_BUBBLE_R       3   /* 圆角半径（px），1bit 屏用切角模拟 */

/* 气泡内文字行数上限与折行宽度缓冲上限 */
#define TT_NAME_LINES     2   /* 课程名最多 2 行 */
#define TT_PLACE_LINES    2   /* 地点最多 2 行（楼名长时可折行） */
#define TT_TEXT_MAX      72   /* 单行可用宽度上限（px），用于折行缓冲定长 */

/* 支持的课数上限 */
#define TT_MAX_COURSES  16

/* 一门课 ----------------------------------------------------------------
 * 用「起始节 + 持续节数」描述跨度：span=1 是单节课，span=2 是常见的跨两节。
 * 同一时段不允许重叠（TT_AddCourse 会拒绝，返回 0）。 */
typedef struct {
    uint8_t  day;                     /* 0~4 = 周一~周五 */
    uint8_t  period;                  /* 起始节：0~4 = 上午1/上午2/下午1/下午2/晚上 */
    uint8_t  span;                    /* 持续节数，>=1；2=跨两节 */
    char     name[TT_COURSE_MAX];     /* 课程名 */
    char     place[TT_PLACE_MAX];     /* 上课地点；空串则显示占位符 */
} Course_t;

/* 课程表数据结构 */
typedef struct {
    Course_t items[TT_MAX_COURSES];
    uint8_t  count;
    uint8_t  week;                    /* 当前第几周（显示在左上角），1~99 */
} Timetable_t;

/* 取全局课表实例（绘制时用） */
Timetable_t* TT_Get(void);

/* 清空课表（所有课移除） */
void TT_Clear(void);

/* 设置当前周次（显示在左上角「第 N 周」），范围 1~99 */
void TT_SetWeek(uint8_t week);

/* 添加一门课；day/period 从 0 起，span 为持续节数（>=1）。
 * 与已有课时间重叠时拒绝添加并返回 0，成功返回 1。 */
int TT_AddCourse(uint8_t day, uint8_t period, uint8_t span,
                 const char *name, const char *place);

/* 在显存里画出整个课表框架（网格线 + 表头 + 节次名）。不刷新屏。 */
void TT_DrawFrame(void);

/* 把课程画成气泡填进格子（在 TT_DrawFrame 之后调用）。不刷新屏。 */
void TT_DrawContent(void);

/* 完整绘制 + 刷新 + 睡眠（一站式调用） */
void TT_Show(void);

#endif /* __TIMETABLE_H */
