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

#define TT_LEFT_W      40     /* 左侧「节次」列宽（像素）。收窄以给课程内容留宽度；
                              * 注意：换成中文节次名（16px/字）时可能需回调。 */
#define TT_HEAD_H      20     /* 表头行高（像素） */

/* 每格宽高（自动算，改上面的参数会自动适配） */
#define TT_CELL_W      ((EPD_WIDTH - TT_LEFT_W) / TT_DAYS)    /* (400-40)/5 = 72 */
#define TT_CELL_H      ((EPD_HEIGHT - TT_HEAD_H) / TT_PERIODS) /* (300-20)/5 = 56 */

/* 单格课程内容长度上限（含结尾 '\0'） */
#define TT_COURSE_MAX  24

/* 课程表数据结构（模块内部持有实例，通过下面的接口读写） */
typedef struct {
    /* course[天][节] = 课程名；空字符串表示没课 */
    char course[TT_DAYS][TT_PERIODS][TT_COURSE_MAX];
} Timetable_t;

/* 清空课表（所有格子置空） */
void TT_Clear(void);

/* 设置某格课程；day/period 从 0 起（day=0 是周一） */
void TT_SetCourse(uint8_t day, uint8_t period, const char *name);

/* 在显存里画出整个课表框架（网格线 + 表头 + 节次名）。不刷新屏。 */
void TT_DrawFrame(void);

/* 把课表内容填进格子（在 TT_DrawFrame 之后调用）。不刷新屏。 */
void TT_DrawContent(void);

/* 完整绘制 + 刷新 + 睡眠（一站式调用） */
void TT_Show(void);

/* ★ 演示：画 5x5 框架 + 几个示例课（先用 ASCII 占位，中文待字库） */
void TT_Demo(void);

#endif /* __TIMETABLE_H */
