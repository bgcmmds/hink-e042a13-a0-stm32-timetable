/**
  ******************************************************************************
  * @file    textlayout.c
  * @brief   UTF-8 文本排版工具（分类：排版模块）
  *
  *          设计要点：
  *            1. 纯计算，不依赖 epd.c —— 可单独测试。
  *            2. 折行结果按「字节偏移」给出，绘制时按范围取，避免二次解码错位。
  *            3. 数字串不拆开（房间号「302」拆成「30/2」很难看）。
  ******************************************************************************
  */
#include "textlayout.h"
#include "font.h"
#include "font_cn.h"

/* 字符在字符串中的偏旁宽度：ASCII 8px，其余（中文）16px */
#define W_ASCII   FONT_ASCII_W
#define W_WIDE    FONT_CN_W
#define ADV_ASCII FONT_ASCII_ADV      /* 含 1px 字距 */
#define ADV_WIDE (FONT_CN_W + 1)

/* 解码一个 UTF-8 字符。返回 Unicode 码点；*nbytes 回填字节数。
 * 非法/不支持的编码统一返回 0xFFFD（替换字符），由调用方跳过。 */
static uint16_t TL_NextChar(const uint8_t *p, int *nbytes)
{
    uint8_t c = p[0];
    if (c < 0x80) {                                  /* ASCII */
        *nbytes = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {                        /* 2 字节 */
        *nbytes = 2;
        return (uint16_t)(((c & 0x1F) << 6) | (p[1] & 0x3F));
    }
    if ((c & 0xF0) == 0xE0) {                        /* 3 字节：汉字在这里 */
        *nbytes = 3;
        return (uint16_t)(((c & 0x0F) << 12) |
                          ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
    }
    if ((c & 0xF8) == 0xF0) {                        /* 4 字节：字库不支持 */
        *nbytes = 4;
        return 0xFFFD;
    }
    *nbytes = 1;                                     /* 非法首字节 */
    return 0xFFFD;
}

int TL_CharWidth(const uint8_t *p, int *adv, int *nbytes)
{
    if (p[0] == '\0') { *adv = 0; *nbytes = 1; return 0; }
    int nb;
    uint16_t code = TL_NextChar(p, &nb);
    *nbytes = nb;
    if (code == 0xFFFD) { *adv = 0; return 0; }      /* 非法/不支持：跳过 */
    if (code < 0x80) { *adv = ADV_ASCII; return W_ASCII; }
    *adv = ADV_WIDE;
    return W_WIDE;
}

/* 从 p 往前走，返回该字节前面那串 ASCII 数字的起点。
 * 用于「不要把 302 拆开」：断点在数字中间时整体挪到下一行。*/
static const uint8_t *digits_start(const uint8_t *begin, const uint8_t *p)
{
    while (p > begin && p[-1] >= '0' && p[-1] <= '9') p--;
    return p;
}

/* 计算 [begin, end) 的像素宽度（不含末字字距）*/
static int width_of(const uint8_t *begin, const uint8_t *end)
{
    int px = 0;
    const uint8_t *r = begin;
    while (r < end) {
        int adv, nb;
        (void)TL_CharWidth(r, &adv, &nb);
        px += adv;
        r += nb;
    }
    return px > 0 ? px - 1 : 0;                      /* 减去末字字距 */
}

void TL_Wrap(const char *text, int len, int max_w, int max_lines, TL_Lines *out)
{
    const uint8_t *base = (const uint8_t *)text;
    const uint8_t *q = base;
    const uint8_t *end = base + len;

    out->count = 0;

    while (q < end && out->count < max_lines) {
        int i = out->count;
        const uint8_t *line_start = q;
        int px = 0;                                  /* 本行已用宽度（含字距） */

        /* 尽可能多塞字符，直到下一个放不下 */
        while (q < end) {
            int adv, nb;
            int w = TL_CharWidth(q, &adv, &nb);
            if (w == 0) { q += nb; continue; }       /* 非法字节，跳过 */
            if (px + w > max_w) {                    /* 放不下 → 换行 */
                if (px == 0) q += nb;                /* 单字就超宽：丢弃 */
                break;
            }
            px += adv;
            q += nb;
        }

        /* 若断点落在数字串中间，把整串挪到下一行（本行相应缩短）*/
        const uint8_t *line_end = q;
        if (q < end && px > 0) {
            const uint8_t *ds = digits_start(line_start, q);
            if (ds < q && ds > line_start) {         /* 断在数字里，且本行还有别的字 */
                line_end = ds;
                q = ds;                              /* 下一行从数字起点开始 */
            }
        }

        out->off[i] = (int)(line_start - base);
        out->len[i] = (int)(line_end - line_start);
        out->px [i] = width_of(line_start, line_end);
        out->count++;
    }
}

void TL_DrawLine(const char *text, const TL_Lines *lines, int line,
                 int cx, int y, TL_DrawFn draw, void *ctx)
{
    if (line < 0 || line >= lines->count) return;

    const uint8_t *base = (const uint8_t *)text + lines->off[line];
    const uint8_t *end  = base + lines->len[line];

    int x = cx - lines->px[line] / 2;                /* 水平居中 */
    const uint8_t *p = base;
    while (p < end) {
        int adv, nb;
        int w = TL_CharWidth(p, &adv, &nb);
        if (w == 0) { p += nb; continue; }
        uint16_t code = TL_NextChar(p, &nb);
        draw(code, x, y, ctx);
        x += adv;
        p += nb;
    }
}
