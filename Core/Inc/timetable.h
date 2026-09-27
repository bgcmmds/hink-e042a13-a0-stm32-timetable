/**
  ******************************************************************************
  * @file    timetable.h
  * @brief   课程表数据模型与绘制接口（分类：业务模块）
  *
  *          布局：5 天（周一~周五）× 5 大节
  *            上午一 / 上午二 / 下午一 / 下午二 / 晚上
  *
  *          屏 400x300 的尺寸分配：
  *            ┌──────┬────┬────┬────┬────┬────┐  ← 表头 20px
  *            │ 3周  │ 一 │ 二 │ 三 │ 四 │ 五 │
  *            ├──────┼────┼────┼────┼────┼────┤
  *            │ 上午一│    │    │    │    │    │  ← 每行 56px
  *            │ 上午二│    │    │    │    │    │
  *            │ 下午一│    │    │    │    │    │
  *            │ 下午二│    │    │    │    │    │
  *            │ 晚上 │    │    │    │    │    │
  *            └──────┴────┴────┴────┴────┴────┘
  *              52px  ← 每列 69px →
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
#define TT_CELL_W      ((EPD_WIDTH - TT_LEFT_W) / TT_DAYS)     /* (400-52)/5 = 69 */
#define TT_CELL_H      ((EPD_HEIGHT - TT_HEAD_H) / TT_PERIODS) /* (300-20)/5 = 56 */

/* 字段长度上限（含结尾 '\0'） */
#define TT_COURSE_MAX  24     /* 课程名 */
#define TT_PLACE_MAX   16     /* 上课地点 */

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
    uint8_t  period;                  /* 起始节：0~4 = 上午一/上午二/下午一/下午二/晚上 */
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

/* 清空课表（所有课移除，周次复位为 1） */
void TT_Clear(void);

/* 设置当前周次（显示在左上角，如「3周」），范围 1~99 */
void TT_SetWeek(uint8_t week);

/* 添加一门课；day/period 从 0 起，span 为持续节数（>=1）。
 * 与已有课时间重叠、或超出 5x5 网格时拒绝添加并返回 0，成功返回 1。 */
int TT_AddCourse(uint8_t day, uint8_t period, uint8_t span,
                 const char *name, const char *place);

/* 绘制整个课表 + 刷新到屏 + 进睡眠（一站式，通常只调这一个）。
 * 数据由 my_courses.c 的 MyCourses_Load() 填好。 */
void TT_Show(void);

#endif /* __TIMETABLE_H */
