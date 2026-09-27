/**
  ******************************************************************************
  * @file    timetable.c
  * @brief   课程表业务逻辑与绘制（分类：业务模块）
  *
  *          只负责「往显存上画」，不负责刷新（刷新由 EPD_Display 做）。
  *          文本排版交给 textlayout.c，本文件只管课表几何与样式。
  *
  *          文件分区：
  *            ① 数据模型      —— 课的增删查、冲突检测
  *            ② 框架绘制      —— 网格线、表头、节次名
  *            ③ 气泡绘制      —— 圆角框 + 色条 + 文字
  *            ④ 对外接口      —— TT_Show（数据由 my_courses.c 提供）
  ******************************************************************************
  */
#include "timetable.h"
#include "textlayout.h"
#include "font.h"
#include "font_cn.h"
#include <string.h>
#include <stdio.h>     /* snprintf：拼「N周」 */

/* 全局课表实例 */
static Timetable_t g_tt;

/* 表头：星期。文字宽度按 2 个汉字算（见 draw_header）。 */
static const char *const kDayName[TT_DAYS] =
    { "周一", "周二", "周三", "周四", "周五" };

/* 行头：节次。3 个汉字，宽度见 draw_header。 */
static const char *const kPeriodName[TT_PERIODS] =
    { "上午一", "上午二", "下午一", "下午二", "晚上" };

static const char *const kNoPlace = "-";     /* 地点缺失时的占位符 */

/* 绘制回调：把单个码点画到显存（textlayout 只负责算位置，落笔在这里）。
 * ctx 传 color（0=黑 1=白）。定义见 ③ 气泡绘制。*/
static void draw_glyph(uint16_t code, int x, int y, void *ctx);


/* ══════════════════════════════════════════════════════════════════════════
 * ① 数据模型
 * ═══════════════════════════════════════════════════════════════════════════*/

void TT_Clear(void)
{
    memset(&g_tt, 0, sizeof(g_tt));
    g_tt.week = 1;                                   /* 默认第 1 周 */
}

void TT_SetWeek(uint8_t week)
{
    if (week < 1)  week = 1;
    if (week > 99) week = 99;
    g_tt.week = week;
}

/* 两个时间段是否重叠（同一天且节次区间相交，区间按开区间处理）*/
static int span_overlap(int day_a, int per_a, int span_a,
                        int day_b, int per_b, int span_b)
{
    if (day_a != day_b) return 0;
    return (per_b < per_a + span_a) && (per_a < per_b + span_b);
}

int TT_AddCourse(uint8_t day, uint8_t period, uint8_t span,
                 const char *name, const char *place)
{
    /* 参数校验：越界或超容量直接拒绝（调用方看返回值）*/
    if (day >= TT_DAYS || period >= TT_PERIODS) return 0;
    if (span < 1) span = 1;
    if (period + span > TT_PERIODS) return 0;
    if (g_tt.count >= TT_MAX_COURSES) return 0;

    /* 冲突检测：同一时段只能有一门课 */
    for (int i = 0; i < g_tt.count; i++) {
        const Course_t *o = &g_tt.items[i];
        if (span_overlap(day, period, span, o->day, o->period, o->span)) return 0;
    }

    Course_t *c = &g_tt.items[g_tt.count++];
    c->day    = day;
    c->period = period;
    c->span   = span;
    strncpy(c->name,  name  ? name  : "", TT_COURSE_MAX - 1);
    c->name[TT_COURSE_MAX - 1]  = '\0';
    strncpy(c->place, place ? place : "", TT_PLACE_MAX  - 1);
    c->place[TT_PLACE_MAX - 1]  = '\0';
    return 1;
}


/* 第 day 天、bound 处的横隔线是否被跨节课程盖住（盖住则该段线不画，
 * 气泡才能上下连成一片）。bound 是「第几条横线」，取值 1..TT_PERIODS-1。*/
static int span_covered(int day, int bound)
{
    for (int i = 0; i < g_tt.count; i++) {
        const Course_t *c = &g_tt.items[i];
        if (c->day == day && bound > c->period && bound < c->period + c->span)
            return 1;
    }
    return 0;
}


/* ══════════════════════════════════════════════════════════════════════════
 * ② 框架绘制
 * ═══════════════════════════════════════════════════════════════════════════*/

/* 画网格线。竖线全高，横线逐列判断（跨节处断开）。*/
static void draw_grid(void)
{
    /* 竖线：左列右边界 + 每天的分隔线。首行区域内是黑底，用白线。*/
    EPD_DrawVLine(TT_LEFT_W, 0, EPD_HEIGHT, 1);
    for (int d = 1; d < TT_DAYS; d++) {
        int x = TT_LEFT_W + d * TT_CELL_W;
        EPD_DrawVLine(x, 0, TT_HEAD_H, 1);
        EPD_DrawVLine(x, TT_HEAD_H, EPD_HEIGHT - TT_HEAD_H, 0);
    }

    /* 横线：表头下边界 + 每节分隔线 */
    EPD_DrawHLine(0, TT_HEAD_H, EPD_WIDTH, 1);
    for (int p = 1; p < TT_PERIODS; p++) {
        int y = TT_HEAD_H + p * TT_CELL_H;
        EPD_DrawHLine(0, y, TT_LEFT_W, 1);               /* 左列黑底上白线 */
        for (int d = 0; d < TT_DAYS; d++) {
            if (span_covered(d, p)) continue;            /* 被气泡盖住 → 断开 */
            /* 左右各留 1px，避免压到竖线 */
            EPD_DrawHLine(TT_LEFT_W + d * TT_CELL_W + 1, y, TT_CELL_W - 1, 0);
        }
    }
}

/* 在反白区域里居中画一行白字（表头/行头共用）。
 * area_x/area_w 是可用区域；宽高都按实际文字算，避免各写一套公式。*/
static void draw_header_cell(int area_x, int area_w, int area_y, int area_h,
                             const char *text)
{
    TL_Lines lines;
    int len = (int)strlen(text);
    TL_Wrap(text, len, area_w, 1, &lines);               /* 表头都是一行，不折 */
    if (lines.count == 0) return;

    int cx = area_x + area_w / 2;                        /* 水平居中 */
    int cy = area_y + (area_h - FONT_CN_H) / 2;          /* 垂直居中 */
    if (cy < area_y) cy = area_y;

    /* 反白区 → 白字：color=1 通过 ctx 传给回调 */
    TL_DrawLine(text, &lines, 0, cx, cy, draw_glyph, (void *)(uintptr_t)1);
}

/* 左上角：显示「N 周」（如「13周」）。缩写是为了两位数周次也能宽松放下
 * —— 该格仅 52x20px，「第13周」会有 51px 贴边。*/
static void draw_week_badge(int week)
{
    if (week < 1)  week = 1;
    if (week > 99) week = 99;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d周", week);
    draw_header_cell(0, TT_LEFT_W, 0, TT_HEAD_H, buf);
}

/* 画整个表格框架：白底 + 反白条带 + 网格线 + 表头文字。不刷新屏。 */
static void draw_frame(void)
{
    /* ① 白底 */
    EPD_Clear(1);

    /* ② 反白条带：首行 + 首列（两块 y 不重叠，交叉格只算首行）*/
    EPD_FillRect(0, 0, EPD_WIDTH, TT_HEAD_H, 0);
    EPD_FillRect(0, TT_HEAD_H, TT_LEFT_W, EPD_HEIGHT - TT_HEAD_H, 0);

    /* ③ 网格线 */
    draw_grid();

    /* ④ 左上角：第几周（占用表头与左列的交叉格，原本是空白）*/
    draw_week_badge(g_tt.week);

    /* ⑤ 表头文字：星期（每列居中）*/
    for (int d = 0; d < TT_DAYS; d++) {
        draw_header_cell(TT_LEFT_W + d * TT_CELL_W, TT_CELL_W, 0, TT_HEAD_H,
                         kDayName[d]);
    }

    /* ⑥ 行头文字：节次（左列居中）*/
    for (int p = 0; p < TT_PERIODS; p++) {
        draw_header_cell(0, TT_LEFT_W, TT_HEAD_H + p * TT_CELL_H, TT_CELL_H,
                         kPeriodName[p]);
    }
}


/* ══════════════════════════════════════════════════════════════════════════
 * ③ 气泡绘制
 * ═══════════════════════════════════════════════════════════════════════════*/

/* 绘制回调：把码点画到显存。ctx 传 color（0=黑 1=白）。*/
static void draw_glyph(uint16_t code, int x, int y, void *ctx)
{
    uint8_t color = (uint8_t)(uintptr_t)ctx;
    if (code < 0x80) EPD_DrawChar(x, y, (uint8_t)code, color);
    else             EPD_DrawChinese(x, y, code, color);
}

/* 圆角矩形边框：四边直线 + 四角圆弧（1bit 屏用切角模拟圆角）*/
static void draw_round_frame(int x, int y, int w, int h, int r, uint8_t color)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    EPD_DrawHLine(x + r, y, w - 2 * r, color);
    EPD_DrawHLine(x + r, y + h - 1, w - 2 * r, color);
    EPD_DrawVLine(x, y + r, h - 2 * r, color);
    EPD_DrawVLine(x + w - 1, y + r, h - 2 * r, color);

    for (int i = 0; i < r; i++) {
        for (int j = 0; j < r; j++) {
            int dx = r - j, dy = r - i;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r + r && d2 > r * r - r) {     /* 只取最外圈 */
                EPD_DrawPixel(x + j,             y + i,             color);
                EPD_DrawPixel(x + w - 1 - j,     y + i,             color);
                EPD_DrawPixel(x + j,             y + h - 1 - i,     color);
                EPD_DrawPixel(x + w - 1 - j,     y + h - 1 - i,     color);
            }
        }
    }
}

/* 地点只保留房间号数字（「主楼302」→「302」）。
 * 楼名占地又占行，而同一门课的楼通常固定，数字才是有用信息。
 * 没有数字（「操场」）原样返回；空串返回占位符。*/
static const char *place_room(const char *place, char *buf, int buf_size)
{
    if (place[0] == '\0') return kNoPlace;

    const char *p = place;
    while (*p && !(*p >= '0' && *p <= '9')) p++;    /* 跳过楼名 */
    if (*p == '\0') return place;                   /* 没数字：原样显示 */

    strncpy(buf, p, buf_size - 1);
    buf[buf_size - 1] = '\0';
    return buf;
}

/* 把课程名 + 地点排进气泡的可用区。
 * 行数按「实际需要」分配：先给课程名，剩下的高度给地点（至少 1 行）。*/
static void draw_bubble_text(const char *name, const char *place,
                             int tx, int ty, int tw, int rows_fit)
{
    TL_Lines name_l, place_l;

    /* 课程名：最多 TT_NAME_LINES 行，但至少给地点留 1 行 */
    int name_max = rows_fit - 1;
    if (name_max > TT_NAME_LINES) name_max = TT_NAME_LINES;
    if (name_max < 1) name_max = 1;
    TL_Wrap(name, (int)strlen(name), tw, name_max, &name_l);

    /* 地点：用剩下的行数 */
    int place_max = rows_fit - name_l.count;
    if (place_max < 1) place_max = 1;
    if (place_max > TT_PLACE_LINES) place_max = TT_PLACE_LINES;
    TL_Wrap(place, (int)strlen(place), tw, place_max, &place_l);

    /* 整块垂直居中 */
    int text_h = name_l.count * FONT_CN_H + (place_l.count ? FONT_CN_H + 2 : 0);
    int y0 = ty + (rows_fit * FONT_CN_H - text_h) / 2;
    if (y0 < ty) y0 = ty;

    int cx = tx + tw / 2;                            /* 文字区水平中心 */

    for (int i = 0; i < name_l.count; i++) {
        TL_DrawLine(name, &name_l, i, cx, y0 + i * FONT_CN_H,
                    draw_glyph, (void *)(uintptr_t)0);
    }
    int place_y = y0 + name_l.count * FONT_CN_H + 2;
    for (int i = 0; i < place_l.count; i++) {
        TL_DrawLine(place, &place_l, i, cx, place_y + i * FONT_CN_H,
                    draw_glyph, (void *)(uintptr_t)0);
    }
}

/* 画一个课程气泡：白底 + 圆角边框 + 左侧色条 + 居中文字。
 * 高度覆盖 span 个节次，跨节课程自然形成一块长气泡。*/
static void draw_bubble(const Course_t *c)
{
    /* 气泡外框：横向占满本列（留 gap 边距），纵向跨 span 个节次 */
    int bx = TT_LEFT_W + c->day * TT_CELL_W + TT_BUBBLE_GAP;
    int by = TT_HEAD_H  + c->period * TT_CELL_H + TT_BUBBLE_GAP;
    int bw = TT_CELL_W - 2 * TT_BUBBLE_GAP;
    int bh = c->span * TT_CELL_H - 2 * TT_BUBBLE_GAP;
    if (bw < 12 || bh < 12) return;                  /* 太小，不画 */

    draw_round_frame(bx, by, bw, bh, TT_BUBBLE_R, 0);

    /* 左侧色条：贴边框内侧，上下让出圆角 */
    EPD_FillRect(bx + 1, by + TT_BUBBLE_R,
                 TT_BUBBLE_BAR_W, bh - 2 * TT_BUBBLE_R, 0);

    /* 文字可用区：色条右侧 + 1px 间隙，右/下留 2px 内边距 */
    int tx = bx + 1 + TT_BUBBLE_BAR_W + 1;
    int tw = (bx + bw - 1 - 2) - tx;
    int rows_fit = (bh - 2) / FONT_CN_H;
    if (tw < 8 || rows_fit < 1) return;
    if (tw > TT_TEXT_MAX) tw = TT_TEXT_MAX;          /* 折行缓冲上限 */

    /* 地点转成房间号，再交给排版 */
    char room_buf[TT_PLACE_MAX];
    const char *room = place_room(c->place, room_buf, sizeof(room_buf));

    draw_bubble_text(c->name, room, tx, by, tw, rows_fit);
}

/* 把所有课程画成气泡。不刷新屏。 */
static void draw_content(void)
{
    for (int i = 0; i < g_tt.count; i++) {
        draw_bubble(&g_tt.items[i]);
    }
}


/* ══════════════════════════════════════════════════════════════════════════
 * ④ 对外接口
 * ═══════════════════════════════════════════════════════════════════════════*/

void TT_Show(void)
{
    draw_frame();
    draw_content();
    EPD_Display();
    EPD_Sleep();
}
