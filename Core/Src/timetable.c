/**
  ******************************************************************************
  * @file    timetable.c
  * @brief   课程表布局与绘制（分类：业务模块）
  *
  *          只负责「往显存上画」，不负责刷新（刷新由 EPD_Display 做）。
  *          这样分层的好处：布局改动不影响屏驱动，屏驱动改动不影响布局。
  ******************************************************************************
  */
#include "timetable.h"
#include "font.h"
#include <string.h>

/* 课表数据实例（本模块私有；通过 TT_SetCourse / TT_Clear 访问） */
static Timetable_t g_tt;

/* 表头：星期（ASCII 占位，等中文字库到位后换「周一」等） */
static const char *kDayName[TT_DAYS] = { "Mon", "Tue", "Wed", "Thu", "Fri" };

/* 行头：节次（同样先 ASCII 占位） */
static const char *kPeriodName[TT_PERIODS] = { "AM1", "AM2", "PM1", "PM2", "Eve" };

void TT_Clear(void)
{
    memset(&g_tt, 0, sizeof(g_tt));
}

void TT_SetCourse(uint8_t day, uint8_t period, const char *name)
{
    if (day >= TT_DAYS || period >= TT_PERIODS) return;
    /* 用 strncpy 并确保结尾有 '\0'，防止超长串溢出 */
    strncpy(g_tt.course[day][period], name, TT_COURSE_MAX - 1);
    g_tt.course[day][period][TT_COURSE_MAX - 1] = '\0';
}

/* ── 画框架：网格线 + 表头 + 节次名 ─────────────────────────────────────────*/
void TT_DrawFrame(void)
{
    /* ① 先整屏清成白底 */
    EPD_Clear(1);

    /* ② 反白（强调表头）：首行 + 首列填黑。
     *    注意两块的 y 范围不重叠（首列从 TT_HEAD_H 开始），
     *    交叉格只属于首行，不会重复涂。 */
    EPD_FillRect(0, 0, EPD_WIDTH, TT_HEAD_H, 0);                     /* 首行条带 */
    EPD_FillRect(0, TT_HEAD_H, TT_LEFT_W, EPD_HEIGHT - TT_HEAD_H, 0);/* 首列条带 */

    /* ③ 网格线：白线画在黑底上，才能看出分隔（反白区内的线要用白色）*/
    /* 竖线：左侧列右边界 + 每天分隔线 */
    EPD_DrawVLine(TT_LEFT_W, 0, EPD_HEIGHT, 1);        /* 黑底上画白线 */
    for (int i = 1; i < TT_DAYS; i++) {
        int x = TT_LEFT_W + i * TT_CELL_W;
        /* 首行区域内的线段用白色（因为在黑底上），其余用黑色 */
        EPD_DrawVLine(x, 0, TT_HEAD_H, 1);
        EPD_DrawVLine(x, TT_HEAD_H, EPD_HEIGHT - TT_HEAD_H, 0);
    }

    /* 横线：表头下边界 + 每节分隔线（所有横向分隔线都在白底区，用黑色）*/
    EPD_DrawHLine(0, TT_HEAD_H, EPD_WIDTH, 1);         /* 首行/首列交界，白线更清晰 */
    for (int i = 1; i < TT_PERIODS; i++) {
        int y = TT_HEAD_H + i * TT_CELL_H;
        EPD_DrawHLine(TT_LEFT_W, y, EPD_WIDTH - TT_LEFT_W, 0);  /* 内容区黑线 */
        EPD_DrawHLine(0, y, TT_LEFT_W, 1);                      /* 首列黑底上白线 */
    }

    /* ④ 表头文字：星期。反白 → 白字(color=1)，居中于每列 */
    for (int d = 0; d < TT_DAYS; d++) {
        int tw = 2 * FONT_ASCII_ADV + FONT_ASCII_W;   /* 3 字符实际宽度 */
        int cx = TT_LEFT_W + d * TT_CELL_W + (TT_CELL_W - tw) / 2;
        int cy = (TT_HEAD_H - FONT_ASCII_H) / 2;
        if (cy < 0) cy = 0;                        /* 表头太矮时贴顶 */
        EPD_DrawString(cx, cy, kDayName[d], 1);
    }

    /* ⑤ 行头文字：节次。反白 → 白字(color=1)，居中于左侧列 */
    for (int p = 0; p < TT_PERIODS; p++) {
        int tw2 = 2 * FONT_ASCII_ADV + FONT_ASCII_W;   /* 3 字符实际宽度 */
        int cx = (TT_LEFT_W - tw2) / 2;
        if (cx < 1) cx = 1;                        /* 列太窄时贴左，防画到屏外 */
        int cy = TT_HEAD_H + p * TT_CELL_H + (TT_CELL_H - FONT_ASCII_H) / 2;
        EPD_DrawString(cx, cy, kPeriodName[p], 1);
    }
}

/* ── 把课程名填进格子（每格最多画 N 行，超长截断） ─────────────────────────
 * ★ 关键：容量用 FONT_ASCII_ADV（步长 = 字宽 + 字距）算，不是字宽 ——
 *   用字宽算会让最后一列压线、溢出到相邻格。
 * -------------------------------------------------------------------------*/
void TT_DrawContent(void)
{
    for (int d = 0; d < TT_DAYS; d++) {
        for (int p = 0; p < TT_PERIODS; p++) {
            const char *name = g_tt.course[d][p];
            if (name[0] == '\0') continue;         /* 空 = 没课 */

            int cell_x = TT_LEFT_W + d * TT_CELL_W;
            int cell_y = TT_HEAD_H + p * TT_CELL_H;

            /* 本格容量：左右各留 2px 边距 */
            int max_chars = (TT_CELL_W - 4) / FONT_ASCII_ADV;
            int max_lines = (TT_CELL_H - 4) / FONT_ASCII_H;
            if (max_chars < 1) max_chars = 1;
            if (max_lines < 1) max_lines = 1;

            int len = (int)strlen(name);
            int line = 0;
            for (int off = 0; off < len && line < max_lines; off += max_chars, line++) {
                int n = len - off;
                if (n > max_chars) n = max_chars;

                /* 逐字符画，超出本格右边界就停止（双保险，防越界） */
                int cx = cell_x + 2;
                int cy = cell_y + 2 + line * FONT_ASCII_H;
                for (int k = 0; k < n; k++) {
                    if (cx + FONT_ASCII_W > cell_x + TT_CELL_W - 2) break;
                    EPD_DrawChar(cx, cy, (uint8_t)name[off + k], 0);
                    cx += FONT_ASCII_ADV;
                }
            }
        }
    }
}

/* ── 完整显示：画 + 刷新 + 睡眠 ─────────────────────────────────────────────*/
void TT_Show(void)
{
    TT_DrawFrame();
    TT_DrawContent();
    EPD_Display();
    EPD_Sleep();
}
